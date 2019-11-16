/* 
 * Copyright (C) 2007 Jonathan Moore Liles
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* lsmi-mouse.c
 *
 * Linux Pseudo MIDI Input -- Mouse
 * March, 2007
 *
 * This driver is capable of generating a stream of MIDI controller and/or note
 * events from the state of mouse buttons. I have a MouseSystems serial mouse
 * controller board with footswitches wired to each of its three buttons. You
 * must have evdev and the kernel driver for your mouse type loaded (in my
 * case, this is sermouse). Mouse axes, wheels, or additional buttons are not
 * used (if you can think of something to do with them [rotary encoders for
 * filter and resonance?], then, by all means, let me know).
 *
 * I use this device to control Freewheeling and various softsynths. Much
 * cheaper than a real MIDI pedalboard, of this I assure you.
 *
 * Example:
 * 
 * 	Use mouse device "/dev/input/event4", mapping left button
 * 	to Controller #64, middle button to Note #36, and
 * 	right button to Note #37 (all on Channel #1):
 * 	
 * 	lsmi-mouse -d /dev/input/event4 -1 c:1:64 -2 n:1:36 -3 n:1:37
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>

#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <stdint.h>

#include <getopt.h>

#include "seq.h"
#include "sig.h"

#define min(x,min) ( (x) < (min) ? (min) : (x) )
#define max(x,max) ( (x) > (max) ? (max) : (x) )

#define testbit(bit, array)    (array[bit/8] & (1<<(bit%8)))


#define CLIENT_NAME "Pseudo-MIDI PS3 Controller"
#define VERSION "0.1"
#define DOWN 1
#define UP 0

char *sub_name = NULL;
int verbose = 0;
int port = 0;
snd_seq_t *seq = NULL;

int daemonize = 0;
int pgm = 0;
char defaultdevice[] = "/dev/input/event2";
char *device = defaultdevice;

/* button mapping */
struct map_s {
	int ev_type;
	unsigned int number;				/* note or controller # */
	unsigned int channel;
};

struct map_s map[22] = {
	//face
	{SND_SEQ_EVENT_NOTEON, 48, 0},
	{SND_SEQ_EVENT_NOTEON, 52, 0},
	{SND_SEQ_EVENT_NOTEON, 55, 0},
	{ SND_SEQ_EVENT_NOTEON, 60, 0 },
	//dpad
	{ SND_SEQ_EVENT_NOTEON, 64, 0 },
	{ SND_SEQ_EVENT_NOTEON, 67, 0 },
	{ SND_SEQ_EVENT_NOTEON, 72, 0 },
	{ SND_SEQ_EVENT_NOTEON, 76, 0 },
	//triggers
	{ SND_SEQ_EVENT_NOTEON, 79, 0 },
	{ SND_SEQ_EVENT_NOTEON, 84, 0 },
	{ SND_SEQ_EVENT_NOTEON, 50, 0 },
	{ SND_SEQ_EVENT_NOTEON, 55, 0 },

	//sticks
	{ SND_SEQ_EVENT_NOTEON, 59, 0 },
	{ SND_SEQ_EVENT_NOTEON, 62, 0 },

	//sticks xy
	{ SND_SEQ_EVENT_PITCHBEND, 0, 0 } ,
	{ SND_SEQ_EVENT_PITCHBEND, 1, 0 } ,
	{ SND_SEQ_EVENT_PITCHBEND, 2, 0 } ,
	{ SND_SEQ_EVENT_PITCHBEND, 3, 0 },
	
	//{ SND_SEQ_EVENT_NOTEON, 65, 0 },
	//{ SND_SEQ_EVENT_NOTEON, 69, 0 },
	//{ SND_SEQ_EVENT_NOTEON, 71, 0 },
	//{ SND_SEQ_EVENT_NOTEON, 74, 0 },

	//Trigger pressure
	{ SND_SEQ_EVENT_NOTEON, 77, 0 },
	{ SND_SEQ_EVENT_NOTEON, 81, 0 },

	//start select

	{ SND_SEQ_EVENT_PGMCHANGE, 81, 0 },
	{ SND_SEQ_EVENT_PGMCHANGE, 81, 0 },
};

int fd;

/**
 * Parse user supplied mapping argument 
 */
void
parse_map ( int i, const char *s )
{
	unsigned char t[2];

	fprintf( stderr, "Applying user supplied mapping...\n" );

	if ( sscanf( s, "%1[cn]:%u:%u", t, &map[i].channel, &map[i].number ) != 3 )
	{
		fprintf( stderr, "Invalid mapping '%s'!\n", s );
		exit( 1 );
	}

	if ( map[i].channel >= 1 && map[i].channel <= 16 )
		map[i].channel--;
	else
	{
		fprintf( stderr, "Channel numbers must be between 1 and 16!\n" );
		exit( 1 );
	}

	if ( map[i].channel > 127 )
	{
		fprintf( stderr, "Controller and note numbers must be between 0 and 127!\n" );
	}
	map[i].ev_type = *t == 'c' ?
		SND_SEQ_EVENT_CONTROLLER : SND_SEQ_EVENT_NOTEON;
}

/** usage
 *
 * print help
 *
 */
void
usage ( void )
{
	fprintf( stderr, "Usage: lsmi-mouse [options]\n"
	"Options:\n\n"
		" -h | --help                   Show this message\n"
		" -d | --device specialfile     Event device to use (instead of event0)\n"
		" -v | --verbose                Be verbose (show note events)\n"
		" -p | --port client:port       Connect to ALSA Sequencer client on startup\n"					

		" -1 | --button-one 'c'|'n':n:n     Button mapping\n"
		" -2 | --button-two 'c'|'n':n:n     Button mapping\n"
		" -3 | --button-thrree 'c'|'n':n:n  Button mapping\n" );
	fprintf( stderr, 	" -z | --daemon                 Fork and don't print anything to stdout\n"
	"\n" );
}

/** 
 * process commandline arguments
 */
void
get_args ( int argc, char **argv )
{
	const char *short_opts = "hp:vd:1:2:3:z";
	const struct option long_opts[] =
	{
		{ "help", no_argument, NULL, 'h' },
		{ "port", required_argument, NULL, 'p' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "device", required_argument, NULL, 'd' },
		{ "button-one", required_argument, NULL, '1' },
		{ "button-two", required_argument, NULL, '2' },
		{ "button-three", required_argument, NULL, '3' },
		{ "daemon", no_argument, NULL, 'z' },
		{ NULL, 0, NULL, 0 }
	};

	int c;

	while ( ( c = getopt_long( argc, argv, short_opts, long_opts, NULL ))
			!= -1 )
	{
		switch (c)
		{
			case 'h':
				usage();
				exit(0);
				break;
			case 'p':
				sub_name = optarg;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'd':
				device = optarg;
				break;
			case '1':
				parse_map( 0, optarg );
				break;
			case '2':
				parse_map( 1, optarg );
				break;
			case '3':
				parse_map( 2, optarg );
				break;
			case 'z':
				daemonize = 1;
				break;
		}
	}
}

/** 
 * Get ready to die gracefully.
 */
void
clean_up ( void )
{
	/* release the mouse */
	ioctl( fd, EVIOCGRAB, 0 );

 	close( fd );

	snd_seq_close( seq );
}

/**
 * Signal handler
 */
void
die ( int sig )
{
	printf( "caught signal %d, cleaning up...\n", sig );
	clean_up();
	exit( 1 );
}

/** 
 * Initialize event device for mouse. 
 */
void
init_mouse ( void )
{
  	uint8_t evt[EV_MAX / 8 + 1];

	/* get capabilities */
	ioctl( fd, EVIOCGBIT( 0, sizeof(evt)), evt );

	if ( ! ( testbit( EV_KEY, evt ) &&
			 testbit( EV_ABS, evt ) ) )
	{
		fprintf( stderr, "'%s' doesn't seem to be a mouse! look in /proc/bus/input/devices to find the name of your mouse's event device\n", device );
		exit( 1 );
	}

	if ( ioctl( fd, EVIOCGRAB, 1 ) )
	{
		perror( "EVIOCGRAB" );
		exit(1);
	}
}


/** main 
 *
 */
int
main ( int argc, char **argv )
{
	snd_seq_event_t ev;
	struct input_event iev;
	snd_seq_addr_t addr;

	fprintf( stderr, "lsmi-mouse" " v" VERSION "\n" );

	get_args( argc, argv );

	fprintf( stderr, "Initializing mouse interface...\n" );

	if ( -1 == ( fd = open( device, O_RDONLY ) ) )
	{
		fprintf( stderr, "Error opening event interface! (%s)\n", strerror( errno ) );
		exit(1);
	}

	init_mouse();

	fprintf( stderr, "Registering MIDI port...\n" );

	seq = open_client( CLIENT_NAME  );
	port = open_output_port( seq );

	if ( sub_name )
	{
		if ( snd_seq_parse_address( seq, &addr, sub_name ) < 0 )
			fprintf( stderr, "Couldn't parse address '%s'", sub_name );
		else
		if ( snd_seq_connect_to( seq, port, addr.client, addr.port ) < 0 )
		{
			fprintf( stderr, "Error creating subscription for port %i:%i", addr.client, addr.port );
			exit( 1 );
		}
	}
	
	if ( daemonize )
	{
		printf( "Running as daemon...\n" );
		if ( fork() )
			exit( 0 );
		else
		{
			fclose( stdout );
			fclose( stderr );
		}
	}

	set_traps();

	fprintf( stderr, "Waiting for packets...\n" );

	for ( ;; )
	{
		int i;

		read( fd, &iev, sizeof( iev ) );

		if ( iev.type != EV_KEY && iev.type != EV_ABS)
			continue;

		switch ( iev.code )
		{
			//Buttons on/off
			//Face buttons
			case BTN_NORTH:		i = 0; break;
			case BTN_SOUTH:	i = 1; break;
			case BTN_EAST:		i = 2; break;
			case BTN_WEST:      i = 3; break;
			//dpad buttons
			case BTN_DPAD_UP: i = 4; break;
			case BTN_DPAD_DOWN: i = 5; break;
			case BTN_DPAD_RIGHT: i = 6; break;
			case BTN_DPAD_LEFT: i = 7; break;
			//triggers
			case BTN_TR: i = 8; break;
			case BTN_TL: i = 9; break;
			case BTN_TR2: i = 10; break;
			case BTN_TL2: i = 11; break;
			//sticks
			case BTN_THUMBR: i = 12; break;
			case BTN_THUMBL: i = 13; break;


			//ABS values
			//Sticks
			case ABS_X: i = 14; break;
			case ABS_Y: i = 15; break;
			case ABS_RX: i = 16; break;
			case ABS_RY: i = 17; break;

			case ABS_Z: i = 18; break;
			case ABS_RZ: i = 19; break;

			case BTN_SELECT: i = 20; break;
			case BTN_START: i = 21; break;


				break;
			default:
				continue;
				break;
		}

		snd_seq_ev_clear( &ev );

		switch ( ev.type = map[i].ev_type )
		{
			case SND_SEQ_EVENT_PITCHBEND:
				snd_seq_ev_set_pitchbend(&ev, map[i].channel,
										(iev.value * 64) - 8192);
				//snd_seq_ev_set_controller( &ev, map[i].channel,
				//								map[i].number,
				//								(iev.value*64) - 8192);
				break;

			case SND_SEQ_EVENT_NOTEON:
				
				snd_seq_ev_set_noteon( &ev, map[i].channel,
											map[i].number,
											iev.value == DOWN ? 127 : 0 );
				break;
			case SND_SEQ_EVENT_PGMCHANGE:
				if (iev.value == 1) {
					pgm = pgm + 1;
					snd_seq_drain_output(seq)
					snd_seq_ev_set_pgmchange(&ev, map[i].channel, pgm);
				}
				else {
					continue;
				}
				break;
			default:
				fprintf( stderr,
						 "Internal error: unexpected mapping type %i !\n.", ev.type);
				continue;
				break;
		}

		send_event( &ev );
	}
}
