#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>

#include "common.h"
#include "fbGUI.h"

long int screensize; // number of bytes in the screen pointer
fb_info fbinfo; // framebuffer information
uint8_t *bkgdp; // pointer to a copy of the screen containing the background

void fb_init()
{
		fbinfo.fbfd = open(FBDEV, O_RDWR);
		if (fbinfo.fbfd < 0) {
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
	if(bkgdp)
		free(bkgdp);
}

void fb_background()
{
	int fd, start, rowsize;
	int width, height, depth, x, y;
	uint8_t *dest,*source;
	pixel *pos;
	struct stat bg_stat;

	if((fd = open(BACKGROUND,O_RDONLY)) < 0)
	{
		//only a warning, default to black background when not found
		WARN("cannot open \"%s\" - %s\n",BACKGROUND,strerror(errno));
		return;
	}
	fstat(fd,&bg_stat); // this will not fail ( open succeded )
	if(!(source = malloc(bg_stat.st_size)))
	{
		FATAL("malloc - %s\n",strerror(errno));
		return;
	}
	start = read(fd,source,bg_stat.st_size); // read it once! ( we need to optimize disk access )
	close(fd);
	memcpy(&start,(source + 10),4);
	memcpy(&width,(source + 18),4);
	memcpy(&height,(source + 22),4);
	memcpy(&depth,(source + 28),2);


	//TODO: handle other formats
	if (depth != BITMAP_DEPTH)
	{
		WARN("Background must be a %i bit .bmp file\n", BITMAP_DEPTH);
		DEBUG("found a %d depth file\n",depth);
		bkgdp = NULL; // this means that we don't have a background
		//return; FIXME: on my background depth will be 196632, but without this check i can draw it fine.
	}

	rowsize = ((BITMAP_DEPTH*width+31)/32)*4; //round to multiple of 4
	rowsize /= sizeof(pixel);
	bkgdp = malloc (screensize);

	if (!bkgdp)
	{
		FATAL("malloc - %s\n",strerror(errno));
		free(source);
		return;
	}


	dest = bkgdp + (fbinfo.vinfo.xoffset)*(fbinfo.vinfo.bits_per_pixel/8) + (fbinfo.vinfo.yoffset)*fbinfo.finfo.line_length;
	pos = ((pixel *)(source + start)) + rowsize*height;
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
	int bytes_per_pixel;
	uint8_t *src,*dst,*pos,*send,*dend,*new_bg, black_pixel[] = {0,0,0,0};

	// exit if we don't have a background
	if(!bkgdp)
		return;

	bytes_per_pixel=(fbinfo.vinfo.bits_per_pixel/8);

	//convert pixels into bytes
	w*=bytes_per_pixel;

	//compute affected area
	area = (h*fbinfo.finfo.line_length);
	//TODO: test this ( it works only if print out of the screen )
	if(x+w>fbinfo.finfo.line_length) {
		x=0;
		w=fbinfo.finfo.line_length;
	}

	if(!(new_bg=malloc(area))) {
		fatal_error=1; // we cannot print, or we will be called infinite times.
		return;
	}

	offset = (x+fbinfo.vinfo.xoffset)*bytes_per_pixel + (y+fbinfo.vinfo.yoffset)*fbinfo.finfo.line_length;
	// save our affected background into new_bg
	memcpy(new_bg,fbinfo.fbp + offset,area);
	src = bkgdp + offset;
	dst = new_bg;
	send = src + area;
	dend = new_bg + area;

	for(;src<send;) {
		//walk the line unntil a black pixel is found
		for(;src<send && memcmp(dst,black_pixel,bytes_per_pixel);src+=bytes_per_pixel,dst+=bytes_per_pixel);
		//walk the line until a non-black pixel is found
		for(pos=dst;pos<dend && !memcmp(pos,black_pixel,bytes_per_pixel);pos+=bytes_per_pixel);
		if(pos>dst)
		{
			memcpy(dst,src,pos-dst);
			src+=(pos-dst);
			dst=pos;
		}
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
