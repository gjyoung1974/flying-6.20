#ifndef _xgraph_h
#define _xgraph_h

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifndef _xsound_h
#	include "xsound.h"
#endif

extern Display	*dpy;
extern int		scr;
extern Pixmap	win;			// depth-8 back buffer, all drawing goes here
extern Window	real_win;	// the visible window
extern Window	new_root;

// copy the back buffer to the visible window (see xgraph.C)
void PresentFrame( int mode=0 );
unsigned long TrueColorPixel( unsigned long pixel8 );

//
// Dirty-rectangle tracking for PresentFrame(): every Xlib drawing call that
// may target the back buffer 'win' is routed through the wrappers below (the
// macros at the end rename the Xlib functions), so that the present step only
// has to fetch the area that actually changed.
//
void MarkDirty( Drawable d, int x, int y, int w, int h );

inline int fly_XCopyPlane( Display *d, Drawable src, Drawable dst, GC gc,
		int sx, int sy, unsigned w, unsigned h, int dx, int dy, unsigned long plane ) {
	MarkDirty( dst, dx, dy, (int)w, (int)h );
	return XCopyPlane( d, src, dst, gc, sx, sy, w, h, dx, dy, plane );
}
inline int fly_XCopyArea( Display *d, Drawable src, Drawable dst, GC gc,
		int sx, int sy, unsigned w, unsigned h, int dx, int dy ) {
	MarkDirty( dst, dx, dy, (int)w, (int)h );
	return XCopyArea( d, src, dst, gc, sx, sy, w, h, dx, dy );
}
inline int fly_XFillRectangle( Display *d, Drawable dr, GC gc,
		int x, int y, unsigned w, unsigned h ) {
	MarkDirty( dr, x, y, (int)w, (int)h );
	return XFillRectangle( d, dr, gc, x, y, w, h );
}
inline int fly_XDrawRectangle( Display *d, Drawable dr, GC gc,
		int x, int y, unsigned w, unsigned h ) {
	MarkDirty( dr, x-1, y-1, (int)w+3, (int)h+3 );
	return XDrawRectangle( d, dr, gc, x, y, w, h );
}
inline int fly_XDrawLine( Display *d, Drawable dr, GC gc, int x1, int y1, int x2, int y2 ) {
	int	lx = (x1<x2)?x1:x2,	hx = (x1<x2)?x2:x1;
	int	ly = (y1<y2)?y1:y2,	hy = (y1<y2)?y2:y1;
	MarkDirty( dr, lx-1, ly-1, hx-lx+3, hy-ly+3 );
	return XDrawLine( d, dr, gc, x1, y1, x2, y2 );
}
inline int fly_XDrawPoint( Display *d, Drawable dr, GC gc, int x, int y ) {
	MarkDirty( dr, x, y, 1, 1 );
	return XDrawPoint( d, dr, gc, x, y );
}
inline int fly_XFillArc( Display *d, Drawable dr, GC gc,
		int x, int y, unsigned w, unsigned h, int a1, int a2 ) {
	MarkDirty( dr, x-1, y-1, (int)w+3, (int)h+3 );
	return XFillArc( d, dr, gc, x, y, w, h, a1, a2 );
}
inline int fly_XDrawArc( Display *d, Drawable dr, GC gc,
		int x, int y, unsigned w, unsigned h, int a1, int a2 ) {
	MarkDirty( dr, x-1, y-1, (int)w+3, (int)h+3 );
	return XDrawArc( d, dr, gc, x, y, w, h, a1, a2 );
}
inline int fly_XFillPolygon( Display *d, Drawable dr, GC gc,
		XPoint *p, int n, int shape, int mode ) {
	if (n>0) {
		int	x = p[0].x, y = p[0].y;
		int	lx = x, hx = x, ly = y, hy = y;
		for (int i=1;i<n;i++) {
			if (mode==CoordModePrevious)	{ x += p[i].x;	y += p[i].y; }
			else									{ x  = p[i].x;	y  = p[i].y; }
			if (x<lx)	lx = x;
			if (x>hx)	hx = x;
			if (y<ly)	ly = y;
			if (y>hy)	hy = y;
		}
		MarkDirty( dr, lx-1, ly-1, hx-lx+3, hy-ly+3 );
	}
	return XFillPolygon( d, dr, gc, p, n, shape, mode );
}

#define XCopyPlane		fly_XCopyPlane
#define XCopyArea			fly_XCopyArea
#define XFillRectangle	fly_XFillRectangle
#define XDrawRectangle	fly_XDrawRectangle
#define XDrawLine			fly_XDrawLine
#define XDrawPoint		fly_XDrawPoint
#define XFillArc			fly_XFillArc
#define XDrawArc			fly_XDrawArc
#define XFillPolygon		fly_XFillPolygon
extern GC		gc_default;

extern GC		gc_col[];

extern GC		gc_lay1;
extern GC		gc_lay2;
extern GC		gc_cursor;

extern GC		gc_current;

extern GC		gc_bxor;
extern GC		gc_bclear;

extern GC		gc_ballwhite;		// für deluxe-Ball
extern GC		gc_ball[];

Window CreateWindow( const char *name, int width, int height );

#if (0)
#ifndef NO_DRAW
inline void FillCircle( double x, double y, double r ) {
	XFillArc( dpy, win, gc_current,
				(int)(x-r), (int)(y-r), (int)(2*r), (int)(2*r),
				0, (int)360*64 );
	XFillArc( dpy, win, gc_lay2,
				(int)((int)(x-r)+r*7/16), (int)((int)(y-r)+r*7/16),
				(int)(r*3/4), (int)(r*3/4),
				0, (int)360*64 );
}
#else
inline void FillCircle( GC *, double, double, double ) { }
#endif
#endif


inline void ClickWall() {
}
inline void ClickBall() {
	do_sound(40,80,1,0);
}
inline void ClickPocket() {
	do_sound(192,80,60,1 );
}

#endif
