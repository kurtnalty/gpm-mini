/*
 * mice.c - mouse definitions for gpm-Linux
 *
 * Copyright (C) 1993        Andrew Haylett <ajh@gec-mrc.co.uk>
 * Copyright (C) 1994-2000   Alessandro Rubini <rubini@linux.it>
 * Copyright (C) 1998,1999   Ian Zimmerman <itz@rahul.net>
 * Copyright (C) 2001-2008   Nico Schottelius <nico-gpm2008 at schottelius.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 ********/

/*
 * This file is part of the mouse server. The information herein
 * is kept aside from the rest of the server to ease fixing mouse-type
 * issues. Each mouse type is expected to fill the `buttons', `dx' and `dy'
 * fields of the Gpm_Event structure and nothing more.
 *
 * Absolute-pointing devices (support by Marc Meis), are expecting to
 * fit `x' and `y' as well. Unfortunately, to do it the window size must
 * be accessed. The global variable "win" is available for that use.
 *
 * The `data' parameter points to a byte-array with event data, as read
 * by the mouse device. The mouse device should return a fixed number of
 * bytes to signal an event, and that exact number is read by the server
 * before calling one of these functions.
 *
 * The conversion function defined here should return 0 on success and -1
 * on failure.
 *
 * Refer to the definition of Gpm_Type to probe further.
 */

#include "../headers/config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h> /* stat() */
#include <sys/time.h> /* select() */

#include <linux/kdev_t.h> /* MAJOR */
#include <linux/keyboard.h>

/* JOYSTICK */
#ifdef HAVE_LINUX_JOYSTICK_H
#include <linux/joystick.h>
#endif

/* EV DEVICE */
#ifdef HAVE_LINUX_INPUT_H
#include <linux/input.h>
#endif /* HAVE_LINUX_INPUT_H */
 


#include "../headers/gpmInt.h"
#include "../headers/daemon.h"
#include "../headers/message.h"


/*========================================================================*/
/* Parsing argv: helper dats struct function (should they get elsewhere?) */
/*========================================================================*/

enum argv_type {
   ARGV_BOOL = 1,
   ARGV_INT, /* "%i" */
   ARGV_DEC, /* "%d" */
   ARGV_STRING,
   /* other types must be added */
   ARGV_END = 0
};

typedef struct argv_helper {
   char *name;
   enum argv_type type;
   union u {
      int *iptr;   /* used for int and bool arguments */
      char **sptr; /* used for string arguments, by strdup()ing the value */
   } u;
   int value; /* used for boolean arguments */
} argv_helper;


/*========================================================================*/
/*  real absolute coordinates for absolute devices,  not very clean */
/*========================================================================*/

#define REALPOS_MAX 16383 /*  min 0 max=16383, but due to change.  */
int realposx=-1, realposy=-1;


/*========================================================================*/
/*
 * Ok, here we are: first, provide the functions; initialization is later.
 * The return value is the number of unprocessed bytes
 */
/*========================================================================*/

/* ps2 */

static int M_ps2(Gpm_Event *state,  unsigned char *data)
{
   static int tap_active=0; /* there exist glidepoint ps2 mice */

   state->buttons=
      !!(data[0]&1) * GPM_B_LEFT +
      !!(data[0]&2) * GPM_B_RIGHT +
      !!(data[0]&4) * GPM_B_MIDDLE;

   if (data[0]==0 && (which_mouse->opt_glidepoint_tap)) /* by default this is false */
      state->buttons = tap_active = (which_mouse->opt_glidepoint_tap);
   else if (tap_active) {
      if (data[0]==8)
         state->buttons = tap_active = 0;
      else
         state->buttons = tap_active;
   }  

   /* Some PS/2 mice send reports with negative bit set in data[0]
    * and zero for movement.  I think this is a bug in the mouse, but
    * working around it only causes artifacts when the actual report is -256;
    * they'll be treated as zero. This should be rare if the mouse sampling
    * rate is set to a reasonable value; the default of 100 Hz is plenty.
    * (Stephen Tell)
    */
   if(data[1] != 0)
      state->dx=   (data[0] & 0x10) ? data[1]-256 : data[1];
   else
      state->dx = 0;
   if(data[2] != 0)
      state->dy= -((data[0] & 0x20) ? data[2]-256 : data[2]);
   else
      state->dy = 0;
   return 0;
}

static int M_imps2(Gpm_Event *state,  unsigned char *data)
{
   
   static int tap_active=0;         /* there exist glidepoint ps2 mice */
   state->wdx  = state->wdy   = 0;  /* Clear them.. */
   state->dx   = state->dy    = state->wdx = state->wdy = 0;

   state->buttons= ((data[0] & 1) << 2)   /* left              */
      | ((data[0] & 6) >> 1);             /* middle and right  */
   
   if (data[0]==0 && (which_mouse->opt_glidepoint_tap)) // by default this is false
      state->buttons = tap_active = (which_mouse->opt_glidepoint_tap);
   else if (tap_active) {
      if (data[0]==8)
         state->buttons = tap_active = 0;
      else state->buttons = tap_active;
   }

   /* Standard movement.. */
   state->dx = (data[0] & 0x10) ? data[1] - 256 : data[1];
   state->dy = (data[0] & 0x20) ? -(data[2] - 256) : -data[2];
   
   /* The wheels.. */
   unsigned char wheel = data[3] & 0x0f;
   if (wheel > 0) {
     // use the event type GPM_MOVE rather than GPM_DOWN for wheel movement
     // to avoid single/double/triple click processing:
     switch (wheel) {
       /* rodney 13/mar/2008
        * The use of GPM_B_UP / GPM_B_DOWN is very unclear; 
        *  only mouse type ms3 uses these
        * For this mouse, we only support the relative movement
        * i.e. no button is set (same as mouse movement), wdy changes +/-
        *  according to wheel movement (+ for rolling away from user)
        * wdx (horizontal scroll) is for a second wheel. They do exist! */
        case 0x0f: state->wdy = +1; break;
        case 0x01: state->wdy = -1; break;
        case 0x0e: state->wdx = +1; break;
        case 0x02: state->wdx = -1; break;
     }
   }

   return 0;

}

/* standard ps2 */
static Gpm_Type *I_ps2(int fd, unsigned short flags,
        struct Gpm_Type *type, int argc, char **argv)
{
   static unsigned char s[] = { 246, 230, 244, 243, 100, 232, 3, };
   write (fd, s, sizeof (s));
   usleep (30000);
   tcflush (fd, TCIFLUSH);
   return type;
}

/*========================================================================*/
/* Then, mice should be initialized */

/*
 * Sends the SEND_ID command to the ps2-type mouse.
 * Return one of GPM_AUX_ID_...
 */
static int read_mouse_id(int fd)
{
   unsigned char c = GPM_AUX_SEND_ID;
   unsigned char id;

   write(fd, &c, 1);
   read(fd, &c, 1);
   if (c != GPM_AUX_ACK) {
      return(GPM_AUX_ID_ERROR);
   }
   read(fd, &id, 1);

   return(id);
}

/**
 * Writes the given data to the ps2-type mouse.
 * Checks for an ACK from each byte.
 * 
 * Returns 0 if OK, or >0 if 1 or more errors occurred.
 */
static int write_to_mouse(int fd, unsigned char *data, size_t len)
{
   int i;
   int error = 0;
   for (i = 0; i < len; i++) {
      unsigned char c;
      write(fd, &data[i], 1);
      read(fd, &c, 1);
      if (c != GPM_AUX_ACK) error++;
   }

   /* flush any left-over input */
   usleep (30000);
   tcflush (fd, TCIFLUSH);
   return(error);
}


/* intellimouse, ps2 version: Ben Pfaff and Colin Plumb */
/* Autodetect: Steve Bennett */
static Gpm_Type *I_imps2(int fd, unsigned short flags, struct Gpm_Type *type,
                                                       int argc, char **argv)
{
   int id;
   static unsigned char basic_init[] = { GPM_AUX_ENABLE_DEV, GPM_AUX_SET_SAMPLE, 100 };
   static unsigned char imps2_init[] = { GPM_AUX_SET_SAMPLE, 200, GPM_AUX_SET_SAMPLE, 100, GPM_AUX_SET_SAMPLE, 80, };
   static unsigned char ps2_init[] = { GPM_AUX_SET_SCALE11, GPM_AUX_ENABLE_DEV, GPM_AUX_SET_SAMPLE, 100, GPM_AUX_SET_RES, 3, };

   /* Do a basic init in case the mouse is confused */
   write_to_mouse(fd, basic_init, sizeof (basic_init));

   /* Now try again and make sure we have a PS/2 mouse */
   if (write_to_mouse(fd, basic_init, sizeof (basic_init)) != 0) {
      gpm_report(GPM_PR_ERR,GPM_MESS_IMPS2_INIT);
      return(NULL);
   }

   /* Try to switch to 3 button mode */
   if (write_to_mouse(fd, imps2_init, sizeof (imps2_init)) != 0) {
      gpm_report(GPM_PR_ERR,GPM_MESS_IMPS2_FAILED);
      return(NULL);
   }

   /* Read the mouse id */
   id = read_mouse_id(fd);
   if (id == GPM_AUX_ID_ERROR) {
      gpm_report(GPM_PR_ERR,GPM_MESS_IMPS2_MID_FAIL);
      id = GPM_AUX_ID_PS2;
   }

   /* And do the real initialisation */
   if (write_to_mouse(fd, ps2_init, sizeof (ps2_init)) != 0) {
      gpm_report(GPM_PR_ERR,GPM_MESS_IMPS2_SETUP_FAIL);
   }

   if (id == GPM_AUX_ID_IMPS2) {
   /* Really an intellipoint, so initialise 3 button mode (4 byte packets) */
      gpm_report(GPM_PR_INFO,GPM_MESS_IMPS2_AUTO);
      return type;
   }
   if (id != GPM_AUX_ID_PS2) {
      gpm_report(GPM_PR_ERR,GPM_MESS_IMPS2_BAD_ID, id);
   }
   else gpm_report(GPM_PR_INFO,GPM_MESS_IMPS2_PS2);

   for (type=mice; type->fun; type++)
      if (strcmp(type->name, "ps2") == 0) return(type);

   /* ps2 was not found!!! */
   return(NULL);
}


/*========================================================================*/
/* Finally, the table */
#define STD_FLG (CREAD|CLOCAL|HUPCL)

/*
 * Note that mman must be the first, and ms the second (I use this info
 * in mouse-test.c, as a quick and dirty hack
 *
 * We should clean up mouse-test and sort the table alphabeticly! --nico
 */

 /*
   * For those who are trying to add a new type, here a brief
   * description of the structure. Please refer to gpmInt.h and gpm.c
   * for more information:
   *
   * The first three strings are the name, an help line, a long name (if any)
   * Then come the functions: the decoder and the initializazion function
   *     (called I_* and M_*)
   * Follows an array of four bytes: it is the protocol-identification, based
   *     on the first two bytes of a packet: if
   *     "((byte0 & proto[0]) == proto[1]) && ((byte1 & proto[2]) == proto[3])"
   *     then we are at the beginning of a packet.
   * The following numbers are:
   *     bytes-per-packet,
   *     bytes-to-read (use 1, bus mice are pathological)
   *     has-extra-byte (boolean, for mman pathological protocol)
   *     is-absolute-coordinates (boolean)
   * Finally, a pointer to a repeater function, if any. - KN - clear out this last field before done.
   */

Gpm_Type mice[]={

   {"imps2","Microsoft Intellimouse (ps2)-autodetect 2/3 buttons,wheel unused",
           "", M_imps2, I_imps2, STD_FLG,
                                {0xC0, 0x00, 0x00, 0x00}, 4, 1, 0, 0, 0},
   {"ps2",  "Busmice of the ps/2 series. Most busmice, actually.",
           "PS/2", M_ps2, I_ps2, STD_FLG,
                                {0xc0, 0x00, 0x00, 0x00}, 3, 1, 0, 0, 0},
   {"",     "",
           "", NULL, NULL, 0,
                                {0x00, 0x00, 0x00, 0x00}, 0, 0, 0, 0, 0}
};

/*------------------------------------------------------------------------*/
/* and the help */

int M_listTypes(void)
{
   Gpm_Type *type;

   printf(GPM_MESS_VERSION "\n");
   printf(GPM_MESS_AVAIL_MYT);
   for (type=mice; type->fun; type++)
      printf(GPM_MESS_SYNONYM, type->repeat_fun?'*':' ',
      type->name, type->desc, type->synonyms);

   putchar('\n');

   return 1; /* to exit() */
}

/* indent: use three spaces. no tab. not two or four. three */

/* Local Variables: */
/* c-indent-level: 3 */
/* End: */
