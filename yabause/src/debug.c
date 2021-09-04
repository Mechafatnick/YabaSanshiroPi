/*  Copyright 2005 Guillaume Duhamel
    Copyright 2006 Theo Berkau

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/
/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file debug.c
    \brief Debug logging functions.
*/

#include "debug.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "osdcore.h"

//////////////////////////////////////////////////////////////////////////////

Debug * DebugInit(const char * n, DebugOutType t, char * s) {
	Debug * d;

        if ((d = (Debug *) malloc(sizeof(Debug))) == NULL)
           return NULL;

	d->output_type = t;

        if ((d->name = strdup(n)) == NULL)
        {
           free(d);
           return NULL;
        }

	switch(t) {
	case DEBUG_STREAM:
                d->output.stream = fopen(s, "w");
		break;
	case DEBUG_STRING:
		d->output.string = s;
		break;
	case DEBUG_STDOUT:
		d->output.stream = stdout;
		break;
	case DEBUG_STDERR:
		d->output.stream = stderr;
		break;
        case DEBUG_CALLBACK:
                d->output.callback = (void  (*) (char*))s;
	        break;
	}

	return d;
}

//////////////////////////////////////////////////////////////////////////////

void DebugDeInit(Debug * d) {
        if (d == NULL)
           return;

	switch(d->output_type) {
	case DEBUG_STREAM:
                if (d->output.stream)
                   fclose(d->output.stream);
		break;
	case DEBUG_STRING:
	case DEBUG_STDOUT:
	case DEBUG_STDERR:
	case DEBUG_CALLBACK:
		break;
	}
        if (d->name)
           free(d->name);
	free(d);
}

//////////////////////////////////////////////////////////////////////////////

void DebugChangeOutput(Debug * d, DebugOutType t, char * s) {
	if (t != d->output_type) {
		if (d->output_type == DEBUG_STREAM)
                {
                   if (d->output.stream)
			fclose(d->output.stream);
                }
		d->output_type = t;
	}
	switch(t) {
	case DEBUG_STREAM:
		d->output.stream = fopen(s, "w");
		break;
	case DEBUG_STRING:
		d->output.string = s;
		break;
	case DEBUG_CALLBACK:
                  d->output.callback = (void  (*) (char*))s;
		  break;
	case DEBUG_STDOUT:
		d->output.stream = stdout;
		break;
	case DEBUG_STDERR:
		d->output.stream = stderr;
		break;
	}
}

#ifdef _WINDOWS
#include <Windows.h>
#endif

//////////////////////////////////////////////////////////////////////////////

void DebugLog( const char * format, ... ) {
  static char strtmp[512];
  int i=0;
  va_list l;
  va_start(l, format);
  i += vsprintf(strtmp + i, format, l);
  OSDAddLogString(strtmp);
  va_end(l);        
}

void DebugPrintf(Debug * d, const char * file, u32 line, const char * format, ...) {
  va_list l;
  static char strtmp[512];
  static int strhash;
  
  if (d == NULL)
    return;
  
  va_start(l, format);
  
  switch(d->output_type) {
  case DEBUG_STDOUT:
  case DEBUG_STDERR:
  case DEBUG_STREAM:
    if (d->output.stream == NULL)
      break;
    fprintf(d->output.stream, "%s (%s:%ld): ", d->name, file, (long)line);
    vfprintf(d->output.stream, format, l);
    break;
  case DEBUG_STRING:
    {
      int i;
      if (d->output.string == NULL)
	break;
      
      i = sprintf(d->output.string, "%s (%s:%ld): ", d->name, file, (long)line);
      vsprintf(d->output.string + i, format, l);
    }
    break;
  case DEBUG_CALLBACK:
    {
      int i=0;
      int strnewhash = 0;
#ifdef _WINDOWS
      static FILE * dfp = NULL;
      if (dfp == NULL){
        dfp = fopen("debug.txt", "w");
      }
#endif
#ifdef ANDROID
      static FILE * dfp = NULL;
      if (dfp == NULL){
        dfp = fopen("/mnt/sdcard/debug.txt", "w");
      }
#endif
      //i = sprintf(strtmp, "%s (%s:%ld): ", d->name, file, (long)line);
      i += vsprintf(strtmp + i, format, l);
     // for ( ; i>0 ; i-- ) strnewhash += (int)(strtmp[i]);
      //if (strnewhash != strhash) {
        //OutputDebugString(strtmp);
        //d->output.callback(strtmp);
        OSDAddLogString(strtmp);
#if defined(ANDROID) 
        fprintf(dfp, "%s\n",strtmp);
        fflush(dfp);
#endif
#if defined(_WINDOWS)
        fprintf(dfp, "%s\n",strtmp);
        fflush(dfp);
#endif
      //}
      //strhash = strnewhash;
    }
    break;
  }
  
  va_end(l);
}

//////////////////////////////////////////////////////////////////////////////

Debug * MainLog;

//////////////////////////////////////////////////////////////////////////////

void LogStart(void) {
        MainLog = DebugInit("main", DEBUG_STDOUT, NULL);
//        MainLog = DebugInit("main", DEBUG_STREAM, "stdout.txt");
}

//////////////////////////////////////////////////////////////////////////////

void LogStop(void) {
	DebugDeInit(MainLog);
	MainLog = NULL;
}

//////////////////////////////////////////////////////////////////////////////

void LogChangeOutput(DebugOutType t, char * s) {

  DebugChangeOutput( MainLog, t, s );
}
