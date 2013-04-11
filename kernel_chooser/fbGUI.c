#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <stdint.h>

#include "common3.h"
#include "fbGUI.h"

long int screensize; // number of bytes in the screen pointer
fb_info fbinfo; // framebuffer information
uint8_t *bkgdp; // pointer to a copy of the screen containing the background

void fb_init()
{
		fbinfo.fbfd = open(FBDEV, O_RDWR);
		if (!fbinfo.fbfd) {
			FATAL("cannot open framebuffer device (%s)\n", FBDEV);
		}
		if (ioctl(fbinfo.fbfd, FBIOGET_FSCREENINFO, &fbinfo.finfo)) {
			FATAL("cannot get screen info\n");
		}
		if (ioctl(fbinfo.fbfd, FBIOGET_VSCREENINFO, &fbinfo.vinfo)) {
			FATAL("cannot get variable screen info\n");
		}

		screensize = fbinfo.vinfo.xres * fbinfo.vinfo.yres * fbinfo.vinfo.bits_per_pixel / 8;

		// map the device to memory
		fbinfo.fbp = (uint8_t  *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbinfo.fbfd, 0);
        if ((int)fbinfo.fbp == -1) {
        	FATAL("failed to map framebuffer device to memory\n");
		}
}

void fb_destroy()
{
	munmap(fbinfo.fbp, screensize);
	close(fbinfo.fbfd);
	free(bkgdp);
}

void fb_background()
{
	int fd, start, rowsize;
	int width, height, x, y;
	uint8_t *dest,*source;
	pixel *pos;
	struct stat bg_stat;

	if(!(fd = open(BACKGROUND,O_RDONLY)))
	{
		//only a warning, default to black background when not found
		WARN("cannot open \"%s\" - %s\n",BACKGROUND,strerror(errno));
		return;
	}
	stat(BACKGROUND,&bg_stat); // this will not fail ( open succeded )
	if(!(source = malloc(bg_stat.st_size)))
	{
		FATAL("malloc - %s\n",strerror(errno));
		return;
	}
	read(fd,source,bg_stat.st_size); // read it once! ( we need to optimize disk access )
	close(fd);
	memcpy(&start,(source + 10),4);
	memcpy(&width,(source + 18),4);
	memcpy(&height,(source + 22),4);

	// @smasher: does it came from empirical measurament? or there is some rule/law ?
	rowsize = ((BITMAP_DEPTH*width+31)/32)*4; //round to multiple of 4
	bkgdp = malloc (screensize);

	if (!bkgdp)
	{
		FATAL("malloc - %s\n",strerror(errno));
		return;
	}

	dest = bkgdp + (fbinfo.vinfo.xoffset)*(fbinfo.vinfo.bits_per_pixel/8) + (fbinfo.vinfo.yoffset)*fbinfo.finfo.line_length;
	pos = (pixel *)(source + start + (rowsize*height));
	for (y=0; y<height; y++) {
		for (x=0; x<width; x++) {
			*dest = pos[x].r;
			*(dest + 1) = pos[x].g;
			*(dest + 2) = pos[x].b;
			*(dest + 3) = 0; //alpha
			dest += fbinfo.vinfo.bits_per_pixel/8; //4
		}
		pos -= rowsize;
	}
	free(source);

	memcpy(fbinfo.fbp, bkgdp, screensize); // copy the background to the screen
}

pixel getpixel(uint8_t *src)
{
	static pixel pix;
	pix.r = *src;
	pix.g = *(src + 1);
	pix.b = *(src + 2);
	return pix;
}

void fb_refresh(int x, int y, int w, int h)
{
	long int offset,area;
	int i,j,sizeof_pixel;
	pixel pix;
	uint8_t *src,*dst,*new_bg,black_pixel[] = {0,0,0,0};

	//compute affected area
	area = (h*fbinfo.fbinfo.line_length) + (x*(fbinfo.vinfo.bits_per_pixel/8));

	if(!(new_bg=malloc(area))) {
		FATAL("malloc - %s\n",strerror(errno));
		return;
	}

	sizeof_pixel = ARRAY_SIZE(black_pixel);
	offset = (x+fbinfo.vinfo.xoffset)*(fbinfo.vinfo.bits_per_pixel/8) + (y+fbinfo.vinfo.yoffset)*fbinfo.finfo.line_length;
	// save our affected background into new_bg
	memcpy(new_bg,fbinfo.fbp + offset,area);
	src = bkgdp + offset;
	dst = new_bg;
	for(j=0;j<h;j++) {
		for(i=0;i<w;i++)
			if(!memcmp(dst +i,black_pixel,sizeof_pixel)) // found a black pixel
				*(dst+i) = *(src+i); // copy from our background
		src += fbinfo.finfo.line_length; //go down a line
		dst += fbinfo.finfo.line_length;
	}
	//write once
	memcpy(fbinfo.fbp + offset,new_bg,area);
	free(new_bg);
}

// wrapper to use row and col for text output
void fb_crefresh(int col, int row, int width, int height)
{
	fb_refresh(col*CHAR_WIDTH, row*CHAR_HEIGHT, width*CHAR_WIDTH, height*CHAR_HEIGHT);
}
