#ifndef _xsound_h
#define _xsound_h

void init_sound( const char *disp_name );
void end_sound( void );
void do_sound( int pitch, int percent, int duration, int pri );

#endif
