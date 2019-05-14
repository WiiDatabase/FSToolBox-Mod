#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/isfs.h>
#include <ogc/ipc.h>
#include <ogc/ios.h>
#include <wiiuse/wpad.h>
#include <ogc/wiilaunch.h>
#include <fcntl.h>
#include <sys/unistd.h>
#include <fat.h>
#include <dirent.h>
#include <sdcard/wiisd_io.h>

#include "sha1.h"
#include "IOSPatch.h"

#define BLOCKSIZE 2048

#define DIRENT_T_FILE 0
#define DIRENT_T_DIR 1

#define INIT_FIRST 1
#define INIT_RESET 0


//Types.

typedef struct _dirent
{
	char name[ISFS_MAXPATH + 1];
	int type;
	u32 ownerID;
	u16 groupID;
	u8 attributes;
	u8 ownerperm;
	u8 groupperm;
	u8 otherperm;
} dirent_t;

typedef struct _dir
{
	char name[ISFS_MAXPATH + 1];
} dir_t;

typedef struct _list
{
	char name[ISFS_MAXPATH + 1];

} list_t;

/* Video pointers */
static GXRModeObj *rmode = NULL;
u32 *xfb;
bool Power_Flag;
bool Reset_Flag;

// Prevent IOS36 loading at startup
s32 __IOS_LoadStartupIOS()
{
	return 0;
}

static void power_cb () 
{
	Power_Flag = true;
}

static void reset_cb () 
{
	Reset_Flag = true;
}

void Verify_Flags()
{
	if (Power_Flag)
	{
		WPAD_Shutdown();
		STM_ShutdownToStandby();
	}
	if (Reset_Flag)
	{
		STM_RebootSystem();
	}
}

void Reboot()
{
	if (*(u32*)0x80001800) exit(0);
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

void waitforbuttonpress(u32 *out, u32 *outGC)
{
	u32 pressed = 0;
	u32 pressedGC = 0;

	while (true)
	{
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0);

		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0);

		if(pressed || pressedGC) 
		{
			if (pressedGC)
			{
				// Without waiting you can't select anything
				usleep (20000);
			}
			if (out) *out = pressed;
			if (outGC) *outGC = pressedGC;
			return;
		}
	}
}

void Con_ClearLine()
{
	s32 cols, rows;
	u32 cnt;

	printf("\r");
	fflush(stdout);

	/* Get console metrics */
	CON_GetMetrics(&cols, &rows);

	/* Erase line */
	for (cnt = 1; cnt < cols; cnt++) {
		printf(" ");
		fflush(stdout);
	}

	printf("\r");
	fflush(stdout);
}

static void sys_init(void)
{
	// Initialise the video system
	VIDEO_Init();
	
	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	
	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);
	
	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);
	
	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	// Set console parameters
    int x = 24, y = 32, w, h;
    w = rmode->fbWidth - (32);
    h = rmode->xfbHeight - (48);

    // Initialize the console - CON_InitEx works after VIDEO_ calls
	CON_InitEx(rmode, x, y, w, h);

	// Clear the garbage around the edges of the console
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
}

void resetscreen()
{
	printf("\x1b[2J");
}

void flash(char* source, char* destination)
{
	u8 *buffer3 = (u8 *)memalign(32, BLOCKSIZE);
	if (buffer3 == NULL)
	{
		printf("Out of memory\n");
		printf("Press any button");
		waitforbuttonpress(NULL, NULL);
		return;
	}

	s32 ret;
	fstats *stats = memalign(32, sizeof(fstats));
	if (stats == NULL)
	{
		printf("Out of memory\n");
		printf("Press any button");
		waitforbuttonpress(NULL, NULL);
		free(buffer3);
		return;
	}

	s32 nandfile;
	FILE *file;
	file = fopen(source, "rb");
	if(!file) 
	{
		printf("fopen error\n");
		printf("Press any button");
		waitforbuttonpress(NULL, NULL);
		free(stats);
		free(buffer3);
		return;
	}
	fseek(file, 0, SEEK_END);
	u32 filesize = ftell(file);
	fseek(file, 0, SEEK_SET);
	printf("Flashing to %s\n", destination);
	printf("SD file is %u bytes\n", filesize);	

	ISFS_Delete(destination);
	ISFS_CreateFile(destination, 0, 3, 3, 3);
	nandfile = ISFS_Open(destination, ISFS_OPEN_RW);
	if(nandfile < 0)
	{
		printf("isfs_open_write error %d\n", nandfile);
		printf("Press any button");
		waitforbuttonpress(NULL, NULL);
		fclose(file);
		free(stats);
		free(buffer3);
		return;
	}
	printf("Writing file to nand...\n\n");
		
	u32 size;
	u32 restsize = filesize;
	while (restsize > 0)
	{
		if (restsize >= BLOCKSIZE)
		{
			size = BLOCKSIZE;
		} else
		{
			size = restsize;
		}
		ret = fread(buffer3, 1, size, file);
		if(!ret) 
		{
			printf(" fread error %d\n", ret);
		}
		ret = ISFS_Write(nandfile, buffer3, size);
		if(!ret) 
		{
			printf("isfs_write error %d\n", ret);
		}
		restsize -= size;
	}
	
	ISFS_Close(nandfile);
	nandfile = ISFS_Open(destination, ISFS_OPEN_RW);
	if(nandfile < 0)
	{
		printf("isfs_open_write error %d\n", nandfile);
		printf("Press any button");
		waitforbuttonpress(NULL, NULL);
		fclose(file);
		free(stats);
		free(buffer3);
		return;
	}	
	
	ret = ISFS_GetFileStats(nandfile, stats);
	printf("Flashing file to nand successful!\n");
	printf("New file is %u bytes\n", stats->file_length);
	ISFS_Close(nandfile);
	fclose(file);
	free(stats);
	free(buffer3);
}

s32 dumpfile(char source[1024], char destination[1024])
{
	Verify_Flags();
	u32 buttonsdown = 0;
	u32 buttonsdownGC = 0;
	
	WPAD_ScanPads();
	buttonsdown = WPAD_ButtonsDown(0);
	PAD_ScanPads();
	buttonsdownGC = PAD_ButtonsDown(0);

	if ((buttonsdown & WPAD_BUTTON_B) || (buttonsdownGC & PAD_BUTTON_B)) 
	{
		printf("\nB button pressed...\n");
		return -1;
	}

	u8 *buffer;
	fstats *status;

	FILE *file;
	int fd;
	s32 ret;
	u32 size;

	fd = ISFS_Open(source, ISFS_OPEN_READ);
	if (fd < 0) 
	{
		printf("\nError: ISFS_OpenFile(%s) returned %d\n", source, fd);
		return fd;
	}
	
	file = fopen(destination, "wb");
	if (!file)
	{
		printf("\nError: fopen(%s) returned 0\n", destination);
		ISFS_Close(fd);
		return -1;
	}

	status = memalign(32, sizeof(fstats) );
	ret = ISFS_GetFileStats(fd, status);
	if (ret < 0)
	{
		printf("\nISFS_GetFileStats(fd) returned %d\n", ret);
		ISFS_Close(fd);
		fclose(file);
		free(status);
		return ret;
	}
	Con_ClearLine();
	printf("Dumping file %s, size = %uKB ...", source, (status->file_length / 1024)+1);

	buffer = (u8 *)memalign(32, BLOCKSIZE);
	u32 restsize = status->file_length;
	while (restsize > 0)
	{
		if (restsize >= BLOCKSIZE)
		{
			size = BLOCKSIZE;
		} else
		{
			size = restsize;
		}
		ret = ISFS_Read(fd, buffer, size);
		if (ret < 0)
		{
			printf("\nISFS_Read(%d, %p, %d) returned %d\n", fd, buffer, size, ret);
			ISFS_Close(fd);
			fclose(file);
			free(status);
			free(buffer);
			return ret;
		}
		ret = fwrite(buffer, 1, size, file);
		if(ret < 0) 
		{
			printf("\nfwrite error%d\n", ret);
			ISFS_Close(fd);
			fclose(file);
			free(status);
			free(buffer);
			return ret;
		}
		restsize -= size;
	}
	ISFS_Close(fd);
	fclose(file);
	free(status);
	free(buffer);
	return 0;
}

void zero_sig(signed_blob *sig) 
{
	u8 *sig_ptr = (u8 *)sig;
	memset(sig_ptr + 4, 0, SIGNATURE_SIZE(sig)-4);
}

void brute_tmd(tmd *p_tmd) 
{
	u16 fill;
	for(fill=0; fill<65535; fill++) 
	{
		p_tmd->fill3=fill;
		sha1 hash;
		//printf("SHA1(%p, %x, %p)\n", p_tmd, TMD_SIZE(p_tmd), hash);
		SHA1((u8 *)p_tmd, TMD_SIZE(p_tmd), hash);;
		  
		if (hash[0]==0) 
		{
			//printf("setting fill3 to %04hx\n", fill);
			return;
		}
	}
	printf("Unable to fix tmd :(\n");
}

			
void sort(u8 *a, int cnt)
{
	u8 tmp;
	int i, j, m;
	
	for(i = 0; i < cnt; i++)
	{
		m = i;
		for(j = i; j < cnt; j++)
			if(a[j] < a[m])
				m = j;
		tmp = a[i];
		a[i] = a[m];
		a[m] = tmp;
	}
}

u8 *get_ioslist(u32 *cnt)
{
	u64 *buf;
	s32 i, res;
	u32 tcnt = 0, icnt = 0;
	u8 *ioses = NULL;
	
	//Get stored IOS versions.
	res = ES_GetNumTitles(&tcnt);
	if(res < 0)
	{
		printf("ES_GetNumTitles: Error! (result = %d)\n", res);
		Reboot();
	}
	buf = memalign(32, sizeof(u64) * tcnt);
	res = ES_GetTitles(buf, tcnt);
	if(res < 0)
	{
		printf("ES_GetTitles: Error! (result = %d)\n", res);
		Reboot();
	}
	//Ugly.
	for(i = 0; i < tcnt; i++)
	{
		if(*((u32 *)(&(buf[i]))) == 1 && (u32)buf[i] > 2 && (u32)buf[i] < 0x100)
		{
			icnt++;
			ioses = (u8 *)realloc(ioses, sizeof(u8) * icnt);
			ioses[icnt - 1] = (u8)buf[i];
		}
	}
	free(buf);
	sort(ioses, icnt);
	
	*cnt = icnt;
	return ioses;
}

int ios_selection(int default_ios)
{
	u32 pressed;
	u32 pressedGC;
	int selection = 0;
	u32 ioscount;
	u8 *list = get_ioslist(&ioscount);
	
	int i;
	for (i=0;i<ioscount;i++)
	{
		if (list[i] == default_ios)
		{
			selection = i;
		}
	}	
	
	while (true)
	{
		printf("\x1B[%d;%dH",0,0);	// move console cursor to x/y
		printf("Select the IOS you want your channel to use: %3d", list[selection]);
		printf("\nThis will not patch it right away so you can cancel it\n");
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0);

		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0);
		if (pressedGC)
		{
			// Without waiting you can't select anything
			usleep (20000);
		}

		if (pressed == WPAD_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
		{	
			if (selection > 0)
			{
				selection--;
			} else
			{
				selection = ioscount - 1;
			}
		}
		if (pressed == WPAD_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
		{
			if (selection < ioscount -1	)
			{
				selection++;
			} else
			{
				selection = 0;
			}
		}
		if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A) break;
	}
	return list[selection];
}


bool patch(char tmdpath[500])
{
	int ios;
	s32 ret;
	s32 nandfile;
	nandfile = ISFS_Open(tmdpath, ISFS_OPEN_RW);
	if (nandfile < 0) 
	{
		printf("Error: ISFS_OpenFile returned %d\n", nandfile);
		return false;
	}	

	fstats *status = memalign( 32, sizeof(fstats) );

	ret = ISFS_GetFileStats(nandfile, status);
	if (ret < 0)
	{
		printf("ISFS_GetFileStats(fd) returned %d\n", ret);
		return false;
	}

	printf("Filesize: %u\n", status->file_length);

	u8 *buffer2 = (u8 *)memalign(32, status->file_length);

	ret = ISFS_Read(nandfile, buffer2, status->file_length);
	if (ret < 0)
	{
		printf("ISFS_Read(%d, %p, %d) returned %d\n", nandfile, buffer2, status->file_length, ret);
		return false;
	}
	ISFS_Close(nandfile);

	resetscreen();
	ios = ios_selection(36);
	printf("\n\nThis channel is using IOS %d\n", buffer2[0x18B]);
	printf("\nPress A to patch TMD to use IOS %d or B to exit\n", ios);

	u32 pressed;
	u32 pressedGC;

	while (true)
	{
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0);
		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0);
			
		if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A)
		{
			break;
		}
		if (pressed == WPAD_BUTTON_B || pressedGC == PAD_BUTTON_B)
		{
			Reboot();
		}
	}
						
	printf("\nPatching TMD to use IOS %d\n", ios);

	zero_sig((signed_blob *)buffer2);
	brute_tmd(SIGNATURE_PAYLOAD((signed_blob *)buffer2));
	//buffer[0x18B] = 0x24;
	buffer2[0x18B] = ios;
	ISFS_Delete(tmdpath);
	ISFS_CreateFile(tmdpath, 0, 3, 3, 1);

	nandfile = ISFS_Open(tmdpath, ISFS_OPEN_RW);
	if (nandfile < 0) 
	{
		printf("Error: ISFS_OpenFile returned %d\n", nandfile);
		return false;
	}	

	//ISFS_Seek(nandfile, 0x0, SEEK_SET);
	ret = ISFS_Write(nandfile, buffer2, status->file_length);
	if(ret < 0) 
	{
		printf("isfs_write error %d\n", ret);
	} else
	{
		printf("Patched succesfully to IOS %d!\n", ios);
	}
	ISFS_Close(nandfile);
	free(status);
	free(buffer2);
	
	return true;
}

int isdir(char *path)
{
	s32 res;
	u32 num = 0;

	res = ISFS_ReadDir(path, NULL, &num);
	if(res < 0)
		return 0;
		
	return 1;
}

void get_attribs(char *path, u32 *ownerID, u16 *groupID, u8 *ownerperm, u8 *groupperm, u8 *otherperm)
{
	s32 res;
	u8 attributes;
	res = ISFS_GetAttr(path, ownerID, groupID, &attributes, ownerperm, groupperm, otherperm);
	if(res != ISFS_OK)
		printf("Error getting attributes of %s! (result = %d)\n", path, res);
}

s32 __FileCmp(const void *a, const void *b)
{
	dirent_t *hdr1 = (dirent_t *)a;
	dirent_t *hdr2 = (dirent_t *)b;
	
	if (hdr1->type == hdr2->type)
	{
		return strcmp(hdr1->name, hdr2->name);
	} else
	{
		if (hdr1->type == DIRENT_T_DIR)
		{
			return -1;
		} else
		{
			return 1;
		}
	}
}

void getdir(char *path, dirent_t **ent, int *cnt)
{
	s32 res;
	u32 num = 0;

	int i, j, k;

	//Get the entry count.
	res = ISFS_ReadDir(path, NULL, &num);
	if(res != ISFS_OK)
	{
		printf("Error: could not get dir entry count! (result: %d)\n", res);
		return;
	}

	//Allocate aligned buffer.
	char *nbuf = (char *)memalign(32, (ISFS_MAXPATH + 1) * num);
	char ebuf[ISFS_MAXPATH + 1];
	char pbuf[ISFS_MAXPATH + 1];

	if(nbuf == NULL)
	{
		printf("Error: could not allocate buffer for name list!\n");
		return;
	}

	//Get the name list.
	res = ISFS_ReadDir(path, nbuf, &num);
	if(res != ISFS_OK)
	{
		printf("Error: could not get name list! (result: %d)\n", res);
		return;
	}
	
	//Set entry cnt.
	*cnt = num;

	if (*ent)
	{
		free(*ent);
	}
	*ent = malloc(sizeof(dirent_t) * num);

	u32 ownerID = 0;
	u16 groupID = 0;
	u8 attributes = 0;
	u8 ownerperm = 0;
	u8 groupperm = 0;
	u8 otherperm = 0;

	//Split up the name list.
	for(i = 0, k = 0; i < num; i++)
	{
		//The names are seperated by zeroes.
		for(j = 0; nbuf[k] != 0; j++, k++)
			ebuf[j] = nbuf[k];
		ebuf[j] = 0;
		k++;

		//Fill in the dir name.
		strcpy((*ent)[i].name, ebuf);
		//Build full path.
		if(strcmp(path, "/") != 0)
			sprintf(pbuf, "%s/%s", path, ebuf);
		else
			sprintf(pbuf, "/%s", ebuf);
		//Dir or file?
		(*ent)[i].type = ((isdir(pbuf) == 1) ? DIRENT_T_DIR : DIRENT_T_FILE);

		res = ISFS_GetAttr(pbuf, &ownerID, &groupID, &attributes, &ownerperm, &groupperm, &otherperm);
		if(res != ISFS_OK)
		{
			ownerID = 0;
			groupID = 0;
			attributes = 0;
			ownerperm = 0;
			groupperm = 0;
			otherperm = 0;
		}
		(*ent)[i].ownerID = ownerID;
		(*ent)[i].groupID = groupID;
		(*ent)[i].attributes = attributes;
		(*ent)[i].ownerperm = ownerperm;
		(*ent)[i].groupperm = groupperm;
		(*ent)[i].otherperm = otherperm;
	}
	qsort(*ent, *cnt, sizeof(dirent_t), __FileCmp);
	
	free(nbuf);
}


void browser(char cpath[ISFS_MAXPATH + 1], dirent_t* ent, int cline, int lcnt)
{
	int i;
	resetscreen();
	printf("Press -/L to dump file to SD, press +/R to write file from SD to NAND\n");
	printf("Press 1/Z to dump the dir you're currently in, including all sub dirs\n");
	printf("press 2/X to patch ios version in a TMD and fakesign it\n\n");
	printf("Path: %s\n\n", cpath);
	printf("  NAME          TYPE     OID     GID     ATTR   OP   GP   OTP\n");
		
	for(i = (cline / 15)*15; i < lcnt && i < (cline / 15)*15+15; i++) 
	{
		printf("%s %-12s  %s   %6u  %6u   %3u    %u    %u    %u\n", 
				(i == cline ? ">" : " "),
				ent[i].name,
				(ent[i].type == DIRENT_T_DIR ? "[DIR] " : "[FILE]"), ent[i].ownerID, ent[i].groupID, ent[i].attributes, ent[i].ownerperm, ent[i].groupperm, ent[i].otherperm				
			);
	}
	printf("\n");
}


bool dumpfolder(char source[1024], char destination[1024])
{
	//printf("Entering folder: %s\n", source);
	
	s32 tcnt;
	int ret;
	int i;
	char path[1024];
	char path2[1024];
	dirent_t *dir = NULL;
	u32 pressed;
	u32 pressedGC;

	getdir(source, &dir, &tcnt);

	if (strlen(source) == 1)
	{
		sprintf(path, "%s", destination);
	} else
	{
		sprintf(path, "%s%s", destination, source);
	}

	ret = (u32)opendir(path);
	if (ret == 0)
	{
		ret = mkdir(path, 0777);
		if (ret < 0)
		{
			printf("Error making directory %d...\n", ret);
			free(dir);
			return false;
		}
	}
	
	for(i = 0; i < tcnt; i++) 
	{				
		if (strlen(source) == 1)
		{
			sprintf(path, "%s%s", source, dir[i].name);
		} else
		{
			sprintf(path, "%s/%s", source, dir[i].name);
		}
		
		if(dir[i].type == DIRENT_T_FILE) 
		{
			sprintf(path2, "%s%s", destination, path);

			//printf("Dumping file: %s\n", path);
			//printf("To: %s\n", path2);

			//sleep(5);
			ret = dumpfile(path, path2);
			if (ret < 0)
			{
				sleep(1);
				printf("Do you want to continue anyway?\n");
				waitforbuttonpress(&pressed, &pressedGC);
				
				if (!((pressed & WPAD_BUTTON_A) || (pressedGC & PAD_BUTTON_A)))
				{
					free(dir);
					return false;
				}
			}
		} else
		{
			if(dir[i].type == DIRENT_T_DIR) 
			{
				if (!dumpfolder(path, destination))
				{
					free(dir);
					return false;
				}
			}	
		}
	}
	free(dir);
	//printf("Dumping folder %s complete\n\n", source);
	return true;
}	

/* int ios_selectionmenu(int default_ios)
{
	u32 pressed;
	u32 pressedGC;
	int selection = 0;
	u32 ioscount;
	u8 *list = get_ioslist(&ioscount);
	
	int i;
	for (i=0;i<ioscount;i++)
	{
		// Default to default_ios if found, else the loaded IOS
		if (list[i] == default_ios)
		{
			selection = i;
			break;
		}
		if (list[i] == IOS_GetVersion())
		{
			selection = i;
		}
	}	
	
	while (true)
	{
		printf("\x1B[%d;%dH",0,0);	// move console cursor to x/y
		printf("Currently using IOS%u (Rev %u)\n", IOS_GetVersion(), IOS_GetRevision());
		printf("Select the IOS to load for FSToolbox : %3d", list[selection]);
		printf("\nIf you want to acces savedata load IOS 249 or a cIOS\n");
		printf("Otherwise IOS 36 is the best choice\n");
		printf("Press B to continue without IOS Reload\n");
	
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0);
		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0);
		if (pressedGC)
		{
			// Without waiting you can't select anything
			usleep (20000);
		}

		if (pressed == WPAD_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
		{	
			if (selection > 0)
			{
				selection--;
			} else
			{
				selection = ioscount - 1;
			}
		}
		if (pressed == WPAD_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
		{
			if (selection < ioscount -1	)
			{
				selection++;
			} else
			{
				selection = 0;
			}
		}
		if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A) break;
		if (pressed == WPAD_BUTTON_B || pressedGC == PAD_BUTTON_B)
		{
			return 0;
		}
		
	}
	return list[selection];
}
*/

/*void config(char filepath[255], char *dat)
{
FILE *f;
f = fopen(filepath, "rb");
char line[100];
char i = fgets(line, 100, f);
while(i != '\n')i++;

*dat = strstr(line, "=") 
					
					;
dat+=2;
while(*dat==' ')dat++;

}*/

int main(int argc, char **argv)
{
	Power_Flag = false;
	Reset_Flag = false;
	SYS_SetPowerCallback (power_cb);
	SYS_SetResetCallback (reset_cb);

	char path2[500];
	char path3[500];
	char tmp[ISFS_MAXPATH + 1];

	s32 ret;
	int i;

	char cpath[ISFS_MAXPATH + 1];	
	dirent_t* ent = NULL;
	int lcnt;
	s32 cline = 0;

	sys_init();

	PAD_Init();
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);

	IOSPATCH_Apply();
	
	sprintf(cpath, "/");

	ret = __io_wiisd.startup();
	if (!ret)
	{
		printf("SD Error\n");
		printf("Press any button to exit\n");
		waitforbuttonpress(NULL, NULL);
		Reboot();
	}
	
	ret = fatMount("sd",&__io_wiisd,0,4,512);
	if (!ret)
	{
		printf("FAT Error\n");
		printf("Press any button to exit\n");
		waitforbuttonpress(NULL, NULL);
		Reboot();
	}

	ret = ISFS_Initialize();
	if (ret < 0) 
	{
		printf("Error: ISFS_Initialize returned %d\n", ret);
		printf("Press any button to exit\n");
		waitforbuttonpress(NULL, NULL);
		Reboot();
	}

	bool accessrights;

	printf("Testing access rights...");
	ret = ISFS_Open("/title/00000001/00000002/content/title.tmd", ISFS_OPEN_RW);
	if (ret >= 0)
	{
		printf("ok.\n");
		accessrights = true;
		ISFS_Close(ret);
	} else
	{
		printf("done.\nEither no system menu installed or access rights are restricted\n");
		accessrights = false;
	}
	
	if (!accessrights)
	{
		printf("\n\nThe used IOS does not have patched nand access, but allowed to use\n");
		printf("ES_Identify. Nand access will be limited.\n");
		printf("Press any button\n");
		waitforbuttonpress(NULL, NULL);
	} else
	{
		sleep(3);
	}
	
	resetscreen();

	printf("FS Toolbox 0.4 by Nicksasa & WiiPower\n\n"); 

	printf("ALL files will be stored in sd:/FSTOOLBOX !\n\n");
	printf("Press -/L to dump file to SD\n");
	printf("Press +/R to write file to nand\n");
	printf("Press 1/Z to dump the dir you're currently in, including all sub dirs\n");
	printf("Press 2/X on a TMD to change the IOS it uses and fakesign it\n");
	printf("Press Home/Start to exit\n\n");

	printf("Press A to continue\n");

	while (1) 
	{
		WPAD_ScanPads();
		u32 buttonsdown = WPAD_ButtonsDown(0);

		PAD_ScanPads();
		u32 buttonsdownGC = PAD_ButtonsDown(0);

		if ((buttonsdown & WPAD_BUTTON_A) || (buttonsdownGC & PAD_BUTTON_A)) 
		{
			break;
		}
		if ((buttonsdown & WPAD_BUTTON_HOME) || (buttonsdownGC & PAD_BUTTON_START)) 
		{
			Reboot();
		}
	}

	ret = (u32)opendir("sd:/FSTOOLBOX");
	if (ret == 0)
	{
		printf("FSTOOLBOX folder doesnt exist, making it...\n");
		ret = mkdir("sd:/FSTOOLBOX", 0777);
		if (ret < 0)
		{
			printf("Error making directory %d...\n", ret);
			sleep(10);
			Reboot();
		}
	}	  
	sleep(2);
	
	resetscreen();
	printf(" This software is in beta stadium\n");
	printf(" A brick prevention methood like BootMii boot2 installation is mandatory\n");
	sleep(1);
	resetscreen();
	sleep(1);
	printf(" This software is in beta stadium\n");
	printf(" A brick prevention methood like BootMii boot2 installation is mandatory\n");
	sleep(1);
	resetscreen();
	sleep(1);
	printf(" This software is in beta stadium\n");
	printf(" A brick prevention methood like BootMii boot2 installation is mandatory\n");
	sleep(1);
	resetscreen();

	getdir(cpath, &ent, &lcnt);
	cline = 0;
	browser(cpath, ent, cline, lcnt);
	
	while (1) 
	{
		Verify_Flags();
		WPAD_ScanPads();
		u32 buttonsdown = WPAD_ButtonsDown(0);
		PAD_ScanPads();
		u32 buttonsdownGC = PAD_ButtonsDown(0);

		if (buttonsdownGC)
		{
			// Without waiting you can't select anything
			usleep (20000);
		}

		//Navigate up.
		if ((buttonsdown & WPAD_BUTTON_UP) || (buttonsdownGC & PAD_BUTTON_UP))
		{			
			if(cline > 0) 
			{
				cline--;
			} else
			{
				cline = lcnt - 1;
			}
			browser(cpath, ent, cline, lcnt);
		}

		//Navigate down.
		if ((buttonsdown & WPAD_BUTTON_DOWN) || (buttonsdownGC & PAD_BUTTON_DOWN))
		{
			if(cline < (lcnt - 1))
			{
				cline++;
			} else
			{
				cline = 0;
			}
			browser(cpath, ent, cline, lcnt);
		}

		//Enter parent dir.
		if ((buttonsdown & WPAD_BUTTON_B) || (buttonsdownGC & PAD_BUTTON_B))
		{
			int len = strlen(cpath);
			for(i = len; cpath[i] != '/'; i--);
			if(i == 0)
				strcpy(cpath, "/");
			else
				cpath[i] = 0;
				
			getdir(cpath, &ent, &lcnt);
			cline = 0;
			browser(cpath, ent, cline, lcnt);
		}

		//Enter dir.
		if ((buttonsdown & WPAD_BUTTON_A) || (buttonsdownGC & PAD_BUTTON_A))
		{
			//Is the current entry a dir?
			if(ent[cline].type == DIRENT_T_DIR)
			{
				strcpy(tmp, cpath);
				if(strcmp(cpath, "/") != 0)
				{
					sprintf(cpath, "%s/%s", tmp, ent[cline].name);
				} else
				{				
					sprintf(cpath, "/%s", ent[cline].name);
				}
				getdir(cpath, &ent, &lcnt);
				cline = 0;
				printf("cline :%s\n", cpath);
				sprintf(path3, "sd:/FSTOOLBOX%s", cpath);
				ret = (u32)opendir(path3);
				if (ret == 0)
				{
					printf("Folder %s does not exist, making it...\n", path3);
					ret = mkdir(path3, 0777);
					if (ret < 0)
					{
						printf("Error making directory %d...\n", ret);
						sleep(10);
						Reboot();
					}
				}
			}
			browser(cpath, ent, cline, lcnt);
		}
		
		//Dump folder
		if ((buttonsdown & WPAD_BUTTON_1) || (buttonsdownGC & PAD_TRIGGER_Z))
		{
			u32 usage1;
			u32 usage2;
			ret = ISFS_GetUsage(cpath, &usage1, &usage2);
			if (ret < 0)
			{
				printf("Couldn't get nand usage\n");
				sleep(10);
				Reboot();
			}
			
			printf("Press A to dump %u MB from: '%s'\n", (usage1 / 64)+1, cpath);
			waitforbuttonpress(&buttonsdown, &buttonsdownGC);
			if ((buttonsdown & WPAD_BUTTON_A) || (buttonsdownGC & PAD_BUTTON_A))
				{
				
				
				if (dumpfolder(cpath, "sd:/FSTOOLBOX"))
				{
					printf("\nDumping complete. Press any button to continue...\n");
				}			
				waitforbuttonpress(NULL, NULL);
			}
			browser(cpath, ent, cline, lcnt);
		}

		//Dump file
		if ((buttonsdown & WPAD_BUTTON_MINUS) || (buttonsdownGC & PAD_TRIGGER_L)) 
		{
			if(ent[cline].type == DIRENT_T_FILE)
			{
				sprintf(tmp, "%s/%s", cpath, ent[cline].name);
				sprintf(path2, "sd:/FSTOOLBOX%s/%s", cpath, ent[cline].name);
			} else
			{
				sprintf(tmp, "/%s", ent[cline].name);
				sprintf(path2, "sd:/FSTOOLBOX%s/%s", cpath, ent[cline].name);
			}
			ret = dumpfile(tmp, path2);
			if (ret >= 0)
			{
				printf("\nDumping complete\n");
			}
		}
		
		//Flash file
		if ((buttonsdown & WPAD_BUTTON_PLUS) || (buttonsdownGC & PAD_TRIGGER_R)) 
		{
			if(ent[cline].type == DIRENT_T_FILE)
			{
				sprintf(tmp, "%s/%s", cpath, ent[cline].name);
				sprintf(path2, "sd:/FSTOOLBOX%s/%s", cpath, ent[cline].name);
			} else
			{
				sprintf(tmp, "/%s", ent[cline].name);
				sprintf(path2, "sd:/FSTOOLBOX%s/%s", cpath, ent[cline].name);
			}
			flash(path2, tmp);
		}
		
		if ((buttonsdown & WPAD_BUTTON_2) || (buttonsdownGC & PAD_BUTTON_X))
		{
			if(ent[cline].type == DIRENT_T_FILE)
			{
				sprintf(tmp, "%s/%s", cpath, ent[cline].name);
			} else
			{
				sprintf(tmp, "/%s", ent[cline].name);
			}
			resetscreen();
			patch(tmp);
			sleep(5);
			browser(cpath, ent, cline, lcnt);
		}

		if ((buttonsdown & WPAD_BUTTON_HOME) || (buttonsdownGC & PAD_BUTTON_START))
		{
			Reboot();
		}
	
	}		
	
}
