
#include "pointer.h"
#include "stipple.h"
#ifndef _xsound_h
#	include "xsound.h"
#endif

#ifndef _game_h
#	include "game.h"
#endif
#ifndef _rgb_h
#	include "rgb.h"
#endif

#include <stdarg.h>

Display	*dpy;			// the display connection
int		scr;			// default screen of the display
Pixmap	win;			// the output drawable: a depth-8 back buffer (see PresentFrame)
Window	real_win;	// the visible window the back buffer is presented in
Window	new_root=0;	// a surrounding black window, when started with option -root (default)
Colormap	cmap;			// the (default) colormap of the screen

static int	nbg_cols_all;
static int	bg_unused;
static int	nball_cols_to_alloc;

Pixmap	pointer_pmap;		// Pixmap (wird vom PBall benutzt)
int		pointer_off_x, pointer_off_y;

static Pixmap		stipple_pmap=0;

// The stipple cache is used to store the 'textured' pixmap to
// reuse some on several output-operation.
#define STIPPLE_CACHE	10
static const int	stipple_sz=128;
static Pixmap		rand_stipple[STIPPLE_CACHE];
static long			rand_col[STIPPLE_CACHE];

static unsigned long	start_seconds;

static int	info_ch = 0;

// ===========================================================================
// Emulation of a writable 8-bit PseudoColor visual on modern X servers.
//
// The drawing model of flying relies on an 8-bit colormap with read/write
// cells and overlay planes: ball colors live in the low bits, the
// background/shade/cursor layers in separate bit planes, and nearly all
// drawing is done with GXxor and plane masks. Modern servers only offer
// TrueColor visuals and no writable cells, so all drawing now goes into a
// depth-8 pixmap ('win'), the colormap is kept in a software palette, and
// PresentFrame() converts the pixmap to TrueColor and copies it into the
// visible window ('real_win').
// ===========================================================================

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#define VPAL_SIZE   256
static XColor         vpal[VPAL_SIZE];         // emulated colormap cells
static unsigned long  vpal_tc[VPAL_SIZE];      // TrueColor pixel per cell
static unsigned char  vpal_valid[VPAL_SIZE];
static XImage        *present_dst = 0;         // TrueColor image for the window
static XImage        *present_src = 0;         // 8-bit image (only with MIT-SHM)
static unsigned char *present_prev = 0;        // previous 8-bit frame (for diffing)
static int            present_prev_valid = 0;
static GC             gc_present = 0;          // GC on real_win (default depth)
static GC             gc_clear8  = 0;          // GC on win (AllPlanes, GXcopy)
static double         present_last = 0.0;
static double         present_last_full = 0.0;
static const double   present_interval = 1.0/60.0;      // max. rate while animating
static const double   present_full_interval = 0.5;      // full refresh when idle

// area of the back buffer drawn to since the last present (see xgraph.h)
static int            dirty_valid = 0;
static int            dirty_x0, dirty_y0, dirty_x1, dirty_y1;

void MarkDirty( Drawable d, int x, int y, int w, int h ) {
	if (d!=win || w<=0 || h<=0)		return;
	int	x1 = x+w, y1 = y+h;
	if (x<0)			x = 0;
	if (y<0)			y = 0;
	if (x1>max_x)	x1 = max_x;
	if (y1>max_y)	y1 = max_y;
	if (x>=x1 || y>=y1)				return;
	if (!dirty_valid) {
		dirty_x0 = x;	dirty_y0 = y;	dirty_x1 = x1;	dirty_y1 = y1;
		dirty_valid = 1;
	}
	else {
		if (x<dirty_x0)	dirty_x0 = x;
		if (y<dirty_y0)	dirty_y0 = y;
		if (x1>dirty_x1)	dirty_x1 = x1;
		if (y1>dirty_y1)	dirty_y1 = y1;
	}
}

// MIT-SHM: avoids copying the whole frame through the socket twice per
// present. Used when the server offers it (i.e. a local display).
static int            present_shm = 0;
static XShmSegmentInfo shm_src_info, shm_dst_info;
static int            shm_error = 0;

static int ShmErrorHandler( Display *, XErrorEvent * ) {
	shm_error = 1;
	return 0;
}

// create an image in shared memory and attach it to the server;
// returns 0 (and cleans up) if anything goes wrong.
static XImage *ShmCreateImage( int depth, int width, int height, XShmSegmentInfo *info ) {
	XImage	*img = XShmCreateImage( dpy, DefaultVisual(dpy,scr), depth, ZPixmap,
												0, info, width, height );
	if (!img)		return 0;

	info->shmid = shmget( IPC_PRIVATE, (size_t)img->bytes_per_line*img->height,
								 IPC_CREAT|0600 );
	if (info->shmid<0) {
		XDestroyImage( img );
		return 0;
	}
	info->shmaddr = img->data = (char*)shmat( info->shmid, 0, 0 );
	info->readOnly = False;
	if (info->shmaddr==(char*)-1) {
		shmctl( info->shmid, IPC_RMID, 0 );
		img->data = 0;
		XDestroyImage( img );
		return 0;
	}

	shm_error = 0;
	int (*old)(Display*,XErrorEvent*) = XSetErrorHandler( ShmErrorHandler );
	int	ok = XShmAttach( dpy, info );
	XSync( dpy, False );
	XSetErrorHandler( old );
	// mark the segment for removal: it disappears when both sides detach
	shmctl( info->shmid, IPC_RMID, 0 );
	if (!ok || shm_error) {
		shmdt( info->shmaddr );
		img->data = 0;
		XDestroyImage( img );
		return 0;
	}
	return img;
}

static void ShmDestroyImage( XImage *img, XShmSegmentInfo *info ) {
	XShmDetach( dpy, info );
	XSync( dpy, False );
	img->data = 0;
	XDestroyImage( img );
	shmdt( info->shmaddr );
}

double GetCurrentTime();

// replacement for XAllocColorCells(): hand out ball colors as the low bits
// and the requested planes as the bits above them.
static int VAllocColorCells( unsigned long *plane_mask, int nplanes,
                             unsigned long *pixels, int ncolors ) {
	int kbits = 0;
	while( (1<<kbits) < ncolors )	kbits++;
	if (kbits+nplanes > 8)			return 0;
	for (int i=0;i<ncolors;i++)	pixels[i] = i;
	for (int i=0;i<nplanes;i++)	plane_mask[i] = 1UL<<(kbits+i);
	for (int i=0;i<VPAL_SIZE;i++)	vpal_valid[i] = 0;
	return 1;
}

// replacement for XStoreColor(): remember the RGB value and look up the
// matching pixel of the real (TrueColor) visual.
static void VStoreColor( XColor *col ) {
	unsigned idx = (unsigned)col->pixel;
	if (idx>=VPAL_SIZE)		return;
	vpal[idx] = *col;
	XColor	t = *col;
	if (XAllocColor( dpy, cmap, &t ))	vpal_tc[idx] = t.pixel;
	else											vpal_tc[idx] = BlackPixel( dpy, scr );
	vpal_valid[idx] = 1;
}

unsigned long TrueColorPixel( unsigned long pixel8 ) {
	if (pixel8<VPAL_SIZE && vpal_valid[pixel8])	return vpal_tc[pixel8];
	return BlackPixel( dpy, scr );
}

static int HostIsLSBFirst() {
	union { unsigned int u; unsigned char c[4]; } t;
	t.u = 1;
	return t.c[0]==1;
}

static void InitPresent() {
	if (present_dst)		return;

	int	bpp = 32;
	int	nfmt;
	XPixmapFormatValues	*fmt = XListPixmapFormats( dpy, &nfmt );
	for (int i=0;fmt&&i<nfmt;i++) {
		if (fmt[i].depth==DefaultDepth(dpy,scr))	bpp = fmt[i].bits_per_pixel;
	}
	if (fmt)		XFree( fmt );

	present_shm = 0;
	if (XShmQueryExtension( dpy )) {
		present_src = ShmCreateImage( 8, max_x, max_y, &shm_src_info );
		if (present_src) {
			present_dst = ShmCreateImage( DefaultDepth(dpy,scr), max_x, max_y, &shm_dst_info );
			if (!present_dst) {
				ShmDestroyImage( present_src, &shm_src_info );
				present_src = 0;
			}
		}
		present_shm = (present_dst!=0);
	}

	if (!present_shm) {
		char	*data = (char*)calloc( (size_t)max_x*max_y, bpp/8 );
		present_dst = XCreateImage( dpy, DefaultVisual(dpy,scr), DefaultDepth(dpy,scr),
					ZPixmap, 0, data, max_x, max_y, 32, 0 );
		if (!present_dst) {
			fprintf( stderr, "can't create image for presenting the frame\n" );
			exit(1);
		}
	}
	present_dst->byte_order = HostIsLSBFirst() ? LSBFirst : MSBFirst;

	present_prev = (unsigned char*)calloc( (size_t)max_x*max_y, 1 );
	present_prev_valid = 0;

	XGCValues	values;
	values.graphics_exposures = False;
	gc_present = XCreateGC( dpy, real_win, GCGraphicsExposures, &values );
}

// convert the 8-bit pixels of 'src' (which covers the back buffer area
// starting at sx,sy) into present_dst and remember them in present_prev
static void ConvertRect( XImage *src, int sx, int sy, int x0, int y0, int x1, int y1 ) {
	for (int y=y0;y<=y1;y++) {
		const unsigned char *s = (const unsigned char*)src->data + (y-sy)*src->bytes_per_line - sx;
		memcpy( present_prev + (size_t)y*max_x + x0, s + x0, x1-x0+1 );
		if (present_dst->bits_per_pixel==32) {
			unsigned int *d = (unsigned int*)(present_dst->data + y*present_dst->bytes_per_line);
			for (int x=x0;x<=x1;x++)	d[x] = (unsigned int)vpal_tc[s[x]];
		}
		else {
			for (int x=x0;x<=x1;x++)	XPutPixel( present_dst, x, y, vpal_tc[s[x]] );
		}
	}
}

static void PutRect( int x0, int y0, int x1, int y1 ) {
	if (present_shm) {
		XShmPutImage( dpy, real_win, gc_present, present_dst,
							x0, y0, x0, y0, x1-x0+1, y1-y0+1, False );
	}
	else {
		XPutImage( dpy, real_win, gc_present, present_dst, x0, y0, x0, y0, x1-x0+1, y1-y0+1 );
	}
}

// fetch the whole back buffer; copy everything (all) or just what differs
// from the last presented frame
static void PresentFull( int all ) {
	XImage	*src;
	if (present_shm) {
		if (!XShmGetImage( dpy, win, present_src, 0, 0, AllPlanes ))	return;
		src = present_src;
	}
	else {
		src = XGetImage( dpy, win, 0, 0, max_x, max_y, AllPlanes, ZPixmap );
		if (!src)					return;
	}

	int	x0 = 0, x1 = max_x-1, y0 = 0, y1 = max_y-1;
	if (!all && present_prev_valid) {
		x0 = max_x;	x1 = -1;	y0 = max_y;	y1 = -1;
		for (int y=0;y<max_y;y++) {
			const unsigned char *s = (const unsigned char*)src->data + y*src->bytes_per_line;
			const unsigned char *p = present_prev + (size_t)y*max_x;
			if (!memcmp( s, p, max_x ))		continue;
			int	a = 0;
			while( s[a]==p[a] )						a++;
			int	b = max_x-1;
			while( s[b]==p[b] )						b--;
			if (a<x0)	x0 = a;
			if (b>x1)	x1 = b;
			if (y<y0)	y0 = y;
			y1 = y;
		}
	}

	if (y1>=y0 && x1>=x0) {
		ConvertRect( src, 0, 0, x0, y0, x1, y1 );
		PutRect( x0, y0, x1, y1 );
	}
	present_prev_valid = 1;
	if (!present_shm)		XDestroyImage( src );
}

// fetch and copy just one rectangle of the back buffer
static void PresentRect( int x0, int y0, int x1, int y1 ) {
	XImage	*src = XGetImage( dpy, win, x0, y0, x1-x0+1, y1-y0+1, AllPlanes, ZPixmap );
	if (!src)						return;
	ConvertRect( src, x0, y0, x0, y0, x1, y1 );
	PutRect( x0, y0, x1, y1 );
	XDestroyImage( src );
}

//
// Copy the depth-8 back buffer into the visible window.
//   mode 0: rate limited (at most 60 frames/s), only the area drawn to
//   mode 1: now (about to wait for events); also a full refresh now and then
//   mode 2: now, the whole window (e.g. after an Expose)
//
void PresentFrame( int mode ) {
	if (!win || !real_win)		return;

	InitPresent();
	double	now = GetCurrentTime();

	if (mode==2) {
		PresentFull( 1 );
		dirty_valid = 0;
		present_last = present_last_full = now;
		return;
	}
	if (mode==0 && (now-present_last) < present_interval)		return;

	if (dirty_valid) {
		long	area = (long)(dirty_x1-dirty_x0)*(dirty_y1-dirty_y0);
		if (!present_prev_valid || area*4 > (long)max_x*max_y) {
			PresentFull( 0 );
			present_last_full = now;
		}
		else {
			PresentRect( dirty_x0, dirty_y0, dirty_x1-1, dirty_y1-1 );
		}
		dirty_valid = 0;
		present_last = now;
	}

	// safety net: a complete refresh now and then, in case some drawing
	// operation was not tracked
	if (mode==1 && (now-present_last_full) >= present_full_interval) {
		PresentFull( 0 );
		present_last = present_last_full = now;
	}
}

static void ClosePresent() {
	if (present_shm) {
		if (present_dst)	ShmDestroyImage( present_dst, &shm_dst_info );
		if (present_src)	ShmDestroyImage( present_src, &shm_src_info );
		present_dst = 0;
		present_src = 0;
		present_shm = 0;
	}
	if (present_dst)		{ XDestroyImage( present_dst );	present_dst = 0; }
	if (present_prev)		{ free( present_prev );			present_prev = 0; }
	if (gc_present)		{ XFreeGC( dpy, gc_present );	gc_present = 0; }
	if (gc_clear8)			{ XFreeGC( dpy, gc_clear8 );	gc_clear8 = 0; }
}

#ifdef STATISTICS
static	GC	gc_text;
static int	info_cw = 0;

static XFontStruct *fs = 0;
static Font	info_font;
static unsigned long info_fg, info_bg;
static const int info_width = 16;
static int info_offset = 0;

void showinfo( StatType stat, const char *info ) {
static info_toggle=0;

	if (new_root) {
		int len = strlen(info);
		if (stat==OffsetInfo) {
			if (len) {
				info_toggle^=1;
				if (info_toggle) {
					XSetForeground( dpy, gc_text, info_bg );
					XSetBackground( dpy, gc_text, info_fg );
				}
			}
			else {
				info = "                  ";
				len = 20;
			}
		}
		XDrawImageString( dpy, new_root,
			gc_text,
			info_offset - (stat+1)*info_width*info_cw,
			DisplayHeight(dpy,scr)- 1,
			info, len);

		if ((stat==OffsetInfo) && (info_toggle)) {
			XSetForeground( dpy, gc_text, info_fg );
			XSetBackground( dpy, gc_text, info_bg );
		}
	}
}

#define	GRAPH_SIZE_X	50
#define	GRAPH_SIZE_Y	800
void graphinfo( int value ) {
static int row=-1;
	if (row<0) {
		for (row=0;row<=GRAPH_SIZE_Y;row+=20) {
			for (int i=0;i<=100;i+=20) {
				XDrawPoint(dpy,new_root,gc_text,row,DisplayHeight(dpy,scr)-1-i );
			}
		}
		row=0;
	}
	if (value>120)	value=120;

	XSetForeground( dpy, gc_text, info_bg );
	XDrawLine(dpy, new_root, gc_text,
		row, DisplayHeight(dpy,scr)-1,
		row, DisplayHeight(dpy,scr)-1-GRAPH_SIZE_X);
	XSetForeground( dpy, gc_text, info_fg );
	if (row%20==0) {
		for (int i=0;i<=100;i+=20) {
			XDrawPoint(dpy, new_root, gc_text, row, DisplayHeight(dpy,scr)-1-i );
		}
	}
	XDrawLine(dpy, new_root, gc_text,
		row, DisplayHeight(dpy,scr)-1-value+1,
		row, DisplayHeight(dpy,scr)-1-value-1);
	row++;
	if (row>GRAPH_SIZE_Y)	row=0;
	XDrawLine(dpy, new_root, gc_text,
		row, DisplayHeight(dpy,scr)-1,
		row, DisplayHeight(dpy,scr)-11);
}
#endif


static void InitTime() {
struct timeval start;

	X_GETTIMEOFDAY( &start );				// comes with X11R6 (see stdinc.h)
	start_seconds = start.tv_sec;
}

double GetCurrentTime() {
struct timeval	current;

	X_GETTIMEOFDAY( &current );			// comes with X11R6 (see stdinc.h)
	return( ((double)(current.tv_sec-start_seconds))+(current.tv_usec/1000000.0) );
}


GC		gc_default;

GC		gc_lay1;
GC		gc_lay2;
GC		gc_cursor;

GC		gc_current;

GC		gc_bxor;
GC		gc_bclear;

GC		gc_ballwhite;
GC		gc_ball[MAX_BALL];


void SetBgColor( ColorId col ) {
	gc_current = gc_default;

//
// set the foreground pixel
//
	if (col&BG_MASK) {
		XSetForeground( dpy, gc_current, ball_pix[0]|bg_pix[col&COLOR_MASK] );
	}
	else if (col&STAT_MASK) {
		XSetForeground( dpy, gc_current, stat_pix[col&COLOR_MASK] );
	}
	else {
		XSetForeground( dpy, gc_current, ball_pix[col&COLOR_MASK] );
	}

//
// set background (when in mixed mode)
//
	if (col&MIX_MASK) {
	//
	// set background color
	//
		if ((col>>COLOR_BITS)&BG_MASK) {
			XSetBackground( dpy, gc_current,
								ball_pix[0]|bg_pix[(col>>COLOR_BITS)&COLOR_MASK] );
		}
		else if ((col>>COLOR_BITS)&STAT_MASK) {
			XSetBackground(dpy,gc_current,stat_pix[(col>>COLOR_BITS)&COLOR_MASK]);
		}
		else {
			XSetBackground(dpy,gc_current,ball_pix[(col>>COLOR_BITS)&COLOR_MASK]);
		}

	//
	// set (or create) background stipple
	//
		if (col&RAND_MASK) {
			int i;
			for (i=0;i<STIPPLE_CACHE;i++) {
				if (col==rand_col[i])	break;
				if (rand_col[i]==0)		break;
			}
			if (i==STIPPLE_CACHE) {
				XFreePixmap(dpy,rand_col[0]);			// drop first map
				for (i=0;i<STIPPLE_CACHE-1;i++) {	// shift other maps
					rand_col[i]     = rand_col[i+1];
					rand_stipple[i] = rand_stipple[i+1];
				}
				rand_col[i] = 0;							// mark last cache as unused
			}

			if (!rand_col[i]) {
				// create a new map for the given color
				rand_col[i]    =col;
				rand_stipple[i]=XCreatePixmap(dpy,win,stipple_sz,stipple_sz,1);

				GC	gc = XCreateGC( dpy, rand_stipple[i], 0, 0L );
				XSetFunction(dpy,gc,GXset);
				XFillRectangle(dpy,rand_stipple[i],gc,0,0,stipple_sz,stipple_sz);
				XSetFunction(dpy,gc,GXclear);
				for (int j=stipple_sz*stipple_sz/
								((int)((col&RAND_MASK)>>RAND_SHIFT)+1); j>0;j--) {
					XDrawPoint(dpy,rand_stipple[i],gc,rand()%stipple_sz,rand()%stipple_sz);
				}
				XFreeGC(dpy,gc);
			}
			XSetStipple( dpy, gc_current, rand_stipple[i] );
		}
		else {
			XSetStipple( dpy, gc_current, stipple_pmap );
		}
		XSetFillStyle( dpy, gc_current, FillOpaqueStippled );
	}
	else {
		XSetFillStyle( dpy, gc_current, FillSolid );
	}
}

int ParseColor( const char *name, XColor *col ) {
RGBColor	rgbcol;

	if ( rgbcol.SetColor(name)==0 ) {
			col->red   = rgbcol.red   | (rgbcol.red  <<8);
			col->green = rgbcol.green | (rgbcol.green<<8);
			col->blue  = rgbcol.blue  | (rgbcol.blue <<8);
		//	printf("'%s': %04x/%04x/%04x\n",name,col->red,col->green,col->blue);
		 	return 0;
	}
	if (XParseColor(dpy,cmap,name,col))			return 0;
	else {
		fprintf(stderr,"failed to query RGB-values for '%s'\n",name );
		return -1;
	}
}

int StoreColor( unsigned pixel ) {
			long	stat=-1;
unsigned long	bgc;
			int	nbgc;
unsigned long	shc;
			int	nshc;
unsigned long	cuc;
unsigned long	bc;
			int	nbc;

		bgc = pixel&bg_mask;			// Hintergrund
		for (nbgc=(nbg_cols_all-1)-1;nbgc>=0;nbgc--) {// passenden Index suchen
			if (bg_pix[nbgc]==bgc)		break;
		}
		if ((bgc)&&nbgc>=nbg_cols) {
				stat = nbgc-nbg_cols;				// unbenutztes Pixel -> static
		}

		shc = pixel&shade_mask;		// Schattierung
		for (nshc=0;nshc<nshade_cols;nshc++) {			// passenden Index suchen
			if (shade_pix[nshc]==shc)		break;
		}
		if ((shc)&&nshc>=nshade_cols) {
				printf( "not implemented: shaded %d free for static\n", pixel );
				return -1;		// unbenutztes Pixel
		}

		cuc = pixel&cursor_mask;   // Cursor

		bc  = pixel-bgc-shc-cuc;	// Ballfarbe
		for (nbc=0;nbc<nball_cols_to_alloc;nbc++) {		// passenden Index suchen
			if (ball_pix[nbc]==bc)		break;
		}
		if (nbc>=nball_cols_to_alloc)		return -1;

		if (nbc>=nball_cols) {
			stat = nball_cols*bg_unused + (nbc-nball_cols)*(nbg_cols_all)
					+ ((nbgc>=0)?nbgc+1:0);
		}
		else {
			if (stat>=0)	stat+=(nbc*bg_unused);
		}

XColor 	col;

	if (cuc&&(stat<nstat_cols)) {
		ParseColor(cursor_col, &col );
	}
	else {
		if (stat>=0) {
			if (stat<nstat_cols) {
				ParseColor(stat_col[stat], &col );
				if (!shc) {
					DBG2( ShowColors, "Static %2ld at pixel %3d\n", stat, pixel );
					stat_pix[stat] = pixel;
				}
			}
			else return -1;
		}
		else if (nbc) {
				ParseColor(ball_col[nbc], &col );		// RGB-Werte lesen
		}
		else if (bgc) {
				ParseColor(bg_col[nbgc], &col );		// RGB-Werte lesen
		}
		else	ParseColor(ball_col[0], &col );

		if (shc) {
			XColor	scol;
			ParseColor(shade_col[nshc], &scol );	// RGB-Werte lesen

			col.red   = (col.red  *m[nshc] + scol.red  *d[nshc])/(m[nshc]+d[nshc]);
			col.green = (col.green*m[nshc] + scol.green*d[nshc])/(m[nshc]+d[nshc]);
			col.blue  = (col.blue *m[nshc] + scol.blue *d[nshc])/(m[nshc]+d[nshc]);
		}
	}

	col.pixel = pixel;
	col.flags = DoRed|DoGreen|DoBlue;
	VStoreColor( &col );
	return 0;
}

void StoreColors() {
	for (int i=0;i<256;i++)	StoreColor(i);
}

unsigned long GetBlackPixel() {
unsigned long erg=GetAllocatedPixel( "black" );

	if (erg)		return erg;
	else			return BlackPixel(dpy,scr);
}

void AllocColors() {
unsigned long plane_mask[8];
int	nplanes = 0;
int	n;

	nball_cols_to_alloc = nball_cols;
//
// Ausrechnen, wieviele Planes benötigt werden
//
	n = nbg_cols;		// Planes für Hintergrund sammeln
	while( n )	{ nplanes++;	n>>=1; }
	nbg_cols_all = (1<<nplanes);
	bg_unused    = nbg_cols_all-1-nbg_cols;
	n = nshade_cols;
	while( n )	{ nplanes++;	n>>=1; }
	if (cursor_col)	nplanes++;

//
// Anfordern der Farben
//
	DBG1( ShowColors, "Graphic: %d planes\n", nplanes );
	DBG1( ShowColors, "Graphic: %d ball colors\n", nball_cols );
	DBG1( ShowColors, "Graphic: %d static colors\n", nstat_cols );
	while (nstat_cols>nball_cols*bg_unused
							+(nball_cols_to_alloc-nball_cols)*(nbg_cols_all)) {
		nball_cols_to_alloc++;
	}
	DBG1( ShowColors, "Graphic: %d statics 'between planes'\n", nball_cols*bg_unused );
	DBG1( ShowColors, "Graphic: allocating %d ball colors\n", nball_cols_to_alloc );

	cmap = DefaultColormap( dpy, scr );
	if (VAllocColorCells( plane_mask, nplanes, ball_pix, nball_cols_to_alloc )==0) {
			fprintf(stderr, "not enough colors: " );
			fprintf(stderr, "I need %d colors + %d planes in an 8 bit colormap.\n",
						nball_cols_to_alloc, nplanes );
			exit(0);
	}

//
// Verteilen der Planes auf die Farben
//
int c=0;
	for (n=0;n<nbg_cols+bg_unused;n++) {
		bg_pix[n] = 0;
		if ((n+1)&1)		bg_pix[n]+=plane_mask[c+0];
		if ((n+1)&2)		bg_pix[n]+=plane_mask[c+1];
		if ((n+1)&4)		bg_pix[n]+=plane_mask[c+2];
	}
	n=nbg_cols;
	bg_mask=0;
	while(n)		{ bg_mask+=plane_mask[c++];		n>>=1; }

	for (n=0;n<nshade_cols;n++) {
		shade_pix[n] = 0;
		if ((n+1)&1)		shade_pix[n]+=plane_mask[c+0];
		if ((n+1)&2)		shade_pix[n]+=plane_mask[c+1];
		if ((n+1)&4)		shade_pix[n]+=plane_mask[c+2];
	}
	n=nshade_cols;
	shade_mask=0;
	while(n)		{ shade_mask+=plane_mask[c++];	n>>=1; }

	if (cursor_col) {
		cursor_mask = plane_mask[c];
	}

	ball_mask=0;
	for (n=0;n<nball_cols_to_alloc;n++) {
		ball_mask|=ball_pix[n];
	}
//
// Farben eintragen
//
	StoreColors();							// Farben in Tabelle eintragen
//
// GC's besetzen
//
unsigned long	value_mask;
XGCValues		values;

	value_mask =	GCPlaneMask | GCForeground | GCBackground | GCFunction
					|	GCArcMode | GCStipple | GCFillStyle | GCGraphicsExposures;
	values.graphics_exposures = False;
	values.foreground = 0xffff;
	values.background = 0;
	values.function	= GXxor;
	values.arc_mode	= ArcChord;		// statt ArcPieSlice
	stipple_pmap      =
	values.stipple		= XCreateBitmapFromData( dpy, RootWindow( dpy, scr ),
									stipple_bits, stipple_width, stipple_height );
	values.fill_style	= FillSolid;
#ifdef STATISTICS
	values.font= info_font;
	if (values.font)	value_mask|=GCFont;
#endif

	values.plane_mask  = bg_pix[0];
	gc_lay1 = XCreateGC( dpy, win, value_mask, &values );
	values.plane_mask  = shade_pix[0];
	gc_lay2 = XCreateGC( dpy, win, value_mask, &values );

	values.plane_mask  = cursor_mask;
	gc_cursor = XCreateGC( dpy, win, value_mask, &values );

//
// Default Graphic-Context erzeugen
//
	values.foreground = ball_pix[0];
	values.plane_mask = ball_mask | shade_pix[0];		// Arbeiten in lay2
	values.function   = GXxor;
	gc_bxor = XCreateGC( dpy, win, value_mask, &values );
	values.function   = GXclear;
	gc_bclear = XCreateGC( dpy, win, value_mask, &values );

	values.plane_mask	= ball_mask|shade_mask|bg_mask|cursor_mask;
	values.function	= GXcopy;
	gc_default = XCreateGC( dpy, win, value_mask, &values );

	values.plane_mask	= AllPlanes;
	values.foreground = ball_pix[0];
	gc_clear8 = XCreateGC( dpy, win, value_mask, &values );
	XFillRectangle( dpy, win, gc_clear8, 0, 0, max_x, max_y );	// clear back buffer

	values.plane_mask	= ball_mask;
	gc_ball[0] = XCreateGC( dpy, win, value_mask, &values );
//
// Kontext für den Text
//
#ifdef STATISTICS
	values.foreground = info_fg = TrueColorPixel( ball_pix[0] );
	values.background = info_bg = BlackPixel( dpy, scr );
	values.plane_mask  = AllPlanes;
	gc_text    = XCreateGC( dpy, RootWindow( dpy, scr ), value_mask, &values );

	info_offset = (DisplayWidth(dpy,scr)+max_x)/2;
#endif
//
// GC's für die Ballfarben
//
unsigned long white_pix=0;

	values.fill_style = FillSolid;
	values.plane_mask = ball_mask;
	values.function	= GXxor;
	values.background = 0;
	if (deluxe) {
		white_pix = GetAllocatedPixel( "ivory" );
		if (!white_pix) {
			fprintf( stderr, "white color 'ivory' missing for ball-underground\n");
			exit(-1);
		}
		values.foreground = (white_pix^ball_pix[0]);
		gc_ballwhite=XCreateGC( dpy, win, value_mask, &values );
	}


	for (int i=0;i<nball_ids;i++) {
		ColorId	col = ball_ids[i];

		if (deluxe) values.foreground = white_pix^ball_pix[col&COLOR_MASK];
		else			values.foreground = ball_pix[0]^ball_pix[col&COLOR_MASK];
		if (col&MIX_MASK) {
			values.foreground = ball_pix[0]^ball_pix[(col>>COLOR_BITS)&COLOR_MASK];
			values.fill_style = FillOpaqueStippled;
		}
		else {
			values.fill_style = FillSolid;
		}

		gc_ball[i]=XCreateGC( dpy, win, value_mask, &values );
	}
//
// Pixmaps erzeugen
//
	pointer_pmap = XCreatePixmapFromBitmapData(dpy,win,
			pointer_bits, pointer_width, pointer_height, cursor_mask, 0,
			8 );
	pointer_off_x = pointer_x_hot;
	pointer_off_y = pointer_y_hot;

#ifdef DEBUG
	if (debug&Sync) {
		XSynchronize(dpy,1);
	}
	if ((debug&ShowSubWindow)&&(!new_root)) { 
		XSizeHints				hints;

		hints.flags = PPosition | PSize | PAspect | PMinSize | PMaxSize;
		hints.x = (DisplayWidth(dpy,scr) -max_x)/2;
		hints.y = 0;
		hints.width  = hints.min_width  = hints.max_width  = max_x;
		hints.height = hints.min_height = hints.max_height = max_y;
		XSetWMNormalHints( dpy,win,&hints );
	}
#endif

#ifdef DEBUG
	if (debug&ShowColors) {
Window	help;
const int lpixs_x = 16;
const int lpixs_y = 16;
const int d=16;
const int o=2;

// To overcome a bug? in the fvwm window-manager, the server is now
// synchronized and a pause for a second is taken. I wonder what fvwm
// is doing in that second ...

XSync(dpy,0);
printf( "wait a second, so that even the FVWM gets a change for not crashing\n" );
sleep(1);

		help = CreateWindow("Noname",lpixs_x*(d+o)+1 ,lpixs_y*(d+o)+1);

		for (int px=0;px<lpixs_x;px++) {
			for (int py=0;py<lpixs_y;py++) {
				if (!StoreColor(px+py*lpixs_x)) {
					XSetForeground(dpy,gc_default,px+py*lpixs_x);
					XFillRectangle(dpy,help,gc_default,1+px*(d+o),1+py*(d+o),d,d );
				}
			}
		}
	}
#endif
}


void MapMainWindow() {
XEvent	event;

	XSetWindowBackground( dpy, real_win, TrueColorPixel( ball_pix[0] ) );
	XMapRaised( dpy, real_win );
	if (new_root) {
		XSetWindowBackground( dpy, new_root, BlackPixel( dpy, scr ) );
		XMapRaised( dpy, new_root );
	}
	XWindowEvent( dpy, real_win, ExposureMask, &event );
	XSetInputFocus( dpy, real_win, RevertToNone, CurrentTime );
	PresentFrame( 2 );
	XSync(dpy,0);
}


Window CreateWindow( const char *name, int width, int height )
{
Window	win;
XSetWindowAttributes	attrib;
XSizeHints				hints;
XEvent	event;
static	int	offx=10;
const int frame = 7;

	hints.flags = PPosition | PSize | PAspect | PMinSize | PMaxSize;
	hints.x = DisplayWidth(dpy,scr) -offx-width-frame;
	hints.y = DisplayHeight(dpy,scr)-height-2*frame,
	hints.width  = hints.min_width  = hints.max_width  = width;
	hints.height = hints.min_height = hints.max_height = height;
	offx+=width+2*frame;

		win = XCreateSimpleWindow( dpy, RootWindow(dpy,scr),
					hints.x, hints.y, hints.width, hints.height,
					0, TrueColorPixel( ball_pix[0] ), TrueColorPixel( ball_pix[0] ) );
	attrib.colormap = cmap;
	XChangeWindowAttributes( dpy, win, CWColormap, &attrib );

	XSetWMNormalHints( dpy,win,&hints );

	XStoreName( dpy, win, name );
	XSelectInput( dpy,win,ExposureMask );
	XMapRaised( dpy, win );
	XWindowEvent( dpy, win, ExposureMask, &event );
printf( "window '%s' mapped and raised at (%d,%d) size: (%dx%d)\n", name, hints.x, hints.y, hints.width, hints.height );
	return win;
}

void InitGraphic( double x, double y )
{
XSetWindowAttributes	setwinattr;

#if (SOUND_SUBSYSTEM)
	if (sound_request)		init_sound( "" );
#endif

	dpy = XOpenDisplay( "" );
	if (!dpy) {
		fprintf( stderr, "can't open display\n" );
		exit(0);
	}
	scr = DefaultScreen(dpy);

#ifdef STATISTICS
	fs=XLoadQueryFont(dpy,"5x8");
	if (!fs) {	fs=XLoadQueryFont(dpy,"6x10");
				if (!fs)	fs=XLoadQueryFont(dpy,"fixed");
	}
	if (fs) {
		info_font = fs->fid;
		info_cw   = XTextWidth(fs,"A",1);
		info_ch   = fs->ascent + fs->descent;
	}
#endif

//
// Größe des Fensters bestimmen: Fenster zentrieren
//
int	dp_height = DisplayHeight(dpy,scr)-info_ch;

	world_x = x;
	world_y = y;
	if ( size>0 ) {
		if (size>DisplayWidth(dpy,scr))		size = DisplayWidth(dpy,scr);
		max_x = size;
		max_y = (int)(size/world_x*world_y);
		if (max_y>dp_height) {
			max_y = dp_height;
			size = max_x = (int)(max_y/world_y*world_x);
		}
	}
	else if ( ((double)DisplayWidth(dpy,scr))/dp_height < world_x/world_y ) {
		max_x  = DisplayWidth(dpy,scr);
		max_y = (int)(max_x/world_x*world_y);
	}
	else {
		max_y = dp_height;
		max_x = (int)(max_y/world_y*world_x);
	}

//
	if ( size<=0 ) {
		setwinattr.override_redirect = (no_override)?False:True;
		new_root = XCreateSimpleWindow( dpy, RootWindow( dpy, scr ),
					0, 0, DisplayWidth( dpy,scr ), DisplayHeight( dpy,scr ),
					0, WhitePixel( dpy,scr ), BlackPixel( dpy,scr ) );
		XChangeWindowAttributes( dpy, new_root, CWOverrideRedirect, &setwinattr );

		real_win = XCreateSimpleWindow( dpy, new_root,
					(DisplayWidth( dpy,scr )-max_x)/2,
					(dp_height-max_y)/2,
					max_x, max_y, 0, BlackPixel( dpy,scr ),WhitePixel( dpy,scr ) );
		XSelectInput( dpy, new_root,	FocusChangeMask|KeyPressMask );
		XSelectInput( dpy, real_win, ExposureMask|KeyPressMask );
		setwinattr.backing_store = (0)?Always:NotUseful;
		setwinattr.bit_gravity   = ForgetGravity;;
		XChangeWindowAttributes(dpy,real_win,CWBitGravity|CWBackingStore,&setwinattr );
		w2n = (max_x-2)/world_x;
	}
	else {
		new_root = 0;
		real_win = XCreateSimpleWindow( dpy, RootWindow( dpy, scr ),
					0,0 , max_x, max_y, 2, 0,0 );
		XSelectInput( dpy, real_win, KeyPressMask|ExposureMask);
		setwinattr.backing_store = (0)?Always:NotUseful;
		setwinattr.bit_gravity   = ForgetGravity;;
		XChangeWindowAttributes(dpy,real_win,CWBitGravity|CWBackingStore,&setwinattr );
		w2n = (max_x-2)/world_x;
	}

//
// The back buffer: everything is drawn into this depth-8 pixmap and
// presented into real_win by PresentFrame().
//
	{
		int	nfmt, have8 = 0;
		XPixmapFormatValues	*fmt = XListPixmapFormats( dpy, &nfmt );
		for (int i=0;fmt&&i<nfmt;i++)	if (fmt[i].depth==8)	have8 = 1;
		if (fmt)		XFree( fmt );
		if (!have8) {
			fprintf( stderr, "this X server does not support depth-8 pixmaps\n" );
			exit(1);
		}
	}
	win = XCreatePixmap( dpy, real_win, max_x, max_y, 8 );

//
// BellPitch einstellen
//
	XStoreName( dpy, real_win, "flying" );
	InitTime();					// Initialisierung der Zeitmessung
}

void UnlockGraphic() {
	XUngrabServer(dpy);
	XSync(dpy,0);
	no_server_grabs+=100;
}

void CloseGraphic()
{
int	i;
#ifdef STATISTICS
	if (fs)		XFreeFont(dpy,fs);
#endif
	for (i=0;i<STIPPLE_CACHE;i++) {
		if (rand_stipple[i])		XFreePixmap(dpy,rand_stipple[i]);
		rand_stipple[i]=0;
		rand_col[i]=0;
	}

	XFreePixmap( dpy, pointer_pmap );
	XFreePixmap( dpy, stipple_pmap );

	XFreeGC( dpy, gc_default );

	XFreeGC( dpy, gc_lay1 );
	XFreeGC( dpy, gc_lay2 );
	XFreeGC( dpy, gc_cursor );

	XFreeGC( dpy, gc_bxor );
	XFreeGC( dpy, gc_bclear );

	if (deluxe)		XFreeGC( dpy, gc_ballwhite );
	for (i=0;i<nball_ids;i++) {
		XFreeGC( dpy, gc_ball[i] );
	}

#ifdef STATISTICS
	XFreeGC( dpy, gc_text );
#endif

	ClosePresent();
	XFreePixmap( dpy, win );
	win = 0;

	end_sound();
	XSync( dpy, 0 );
	XCloseDisplay( dpy );
}





char	GetKey()
{
XEvent	event;

	XSync( dpy, 0 );
	PBall::WaitForEvents();

	while (XEventsQueued( dpy, QueuedAfterFlush )) {
		XNextEvent( dpy, &event );
		switch( event.xany.type ) {
		case KeyPress:
		{	char				buffer=0;
			XComposeStatus	compose;
			KeySym			keysym;

			XLookupString( (XKeyEvent*)&event, &buffer, 1, &keysym, &compose );
			if (buffer!='\014') {
				if (isupper(buffer))		return tolower(buffer);
				return buffer;
			}
			// run straight down to the exposure event when ^L was pressed
		}
		case Expose:
		{
			while( XCheckMaskEvent( dpy, ExposureMask, &event ))
											;
			XFillRectangle( dpy, win, gc_clear8, 0, 0, max_x, max_y );
			g->ExposeRedraw();
			PBallTop::ForAll( &PBallTop::Redraw );
			PresentFrame( 2 );
			break;
		}
		case FocusIn:
		//	printf( "focus: In\n" );
			break;
		case FocusOut:
		//	printf( "focus: Out\n" );
			break;
		default:
			break;
		}
	}
	return 0;
}


#define XPix(x)	((int)((x)*w2n))
#define YPix(y)	((int)((y)*w2n))
#define Pix(r)		((int)((r)*w2n))

void DrawLine( const Real& x1, const Real& y1, const Real& x2, const Real& y2 ) {
int	px1 = XPix(x1);
int	py1 = YPix(y1);
int	px2 = XPix(x2);
int	py2 = YPix(y2);
	XDrawLine( dpy, win, gc_current, px1, py1, px2, py2 );
}


void DrawArc( const Real& x, const Real& y, const Real& r, const Real& from, const Real& angle ) {
int	px  = XPix(x-r);
int	py  = YPix(y-r);
int	size= Pix(2.*r);

	XDrawArc( dpy, win, gc_current,
				px, py, size, size,
				(int)(from*64.), (int)(angle*64.) );
}


void DrawCircle( const Real& x, const Real& y, const Real& r ) {
int	px  = XPix(x-r);
int	py  = YPix(y-r);
int	size= Pix(2.*r);

	XDrawArc( dpy, win, gc_current,
				px, py, size, size,
				0, (int)360*64 );
}


void FillArc( const Real& x, const Real& y, const Real& r, const Real& from, const Real& angle ) {
int	px  = XPix(x-r);
int	py  = YPix(y-r);
int	size= Pix(2.*r);

	XFillArc( dpy, win, gc_current,
				px, py, size, size,
				(int)(from*64.), (int)(angle*64.) );
}

void FillCircle( const Real& x, const Real& y, const Real& r ) {
int	px  = XPix(x-r);
int	py  = YPix(y-r);
int	size= Pix(2.*r);

	XFillArc( dpy, win, gc_current,
				px, py, size, size,
				0, (int)360*64 );
}


void FillRectangle( const Real& x, const Real& y, const Real& width, const Real& height ) {
int	px1 = XPix(x);
int	py1 = YPix(y);
int	px2 = XPix(x+width);
int	py2 = YPix(y+height);
	XFillRectangle( dpy, win, gc_current, px1, py1, px2-px1, py2-py1 );
}

void FillPoly( int n, ... ) {
va_list	argptr;
Vec2	*v;
int		i;
XPoint	buffer[10];
XPoint	*p;				// Zeiger auf Puffer, um gegebenenfalls mehr anzufordern

	va_start(argptr,n);

	if (sizeof(buffer)/sizeof(XPoint)<(unsigned)n)	p=new XPoint[n];
	else															p=buffer;

	for (i=0;i<n;i++) {
		v =  va_arg(argptr,Vec2*);
		p[i].x = XPix(v->X());
		p[i].y = YPix(v->Y());

	}
	XFillPolygon( dpy,win,gc_current,p,n,0,0);

	if (p!=buffer)									delete[] p;
	va_end(argptr);
}

void FillPoly( int n, Vec2 v[] ) {
int		i;
XPoint	buffer[10];
XPoint	*p;

	if (sizeof(buffer)/sizeof(XPoint)<(unsigned)n)	p=new XPoint[n];
	else															p=buffer;

	for (i=0;i<n;i++) {
		p[i].x = XPix(v[i].X());
		p[i].y = YPix(v[i].Y());
	}
	XFillPolygon( dpy,win,gc_current,p,n,0,0);

	if (p!=buffer)									delete[] p;
}


