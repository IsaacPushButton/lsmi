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


#define CLIENT_NAME "Pseudo-MIDI Mouse"
#define VERSION "0.1"
#define DOWN 1
#define UP 0

char *sub_name = NULL;
int verbose = 0;
int port = 0;
snd_seq_t *seq = NULL;

int daemonize = 0;

char defaultdevice[] = "/dev/input/event2";
char *device = defaultdevice;

/* button mapping */
struct map_s {
	int ev_type;
	unsigned int number;				/* note or controller # */
	unsigned int channel;
};

struct map_s map[5] = {
	{SND_SEQ_EVENT_CONTROLLER, 64, 0},
	{SND_SEQ_EVENT_NOTEON, 36, 0},
	{SND_SEQ_EVENT_NOTEON, 37, 0},
	{ SND_SEQ_EVENT_NOTEON, 50, 0 },
	{ SND_SEQ_EVENT_NOTEON, 45, 0 },

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
			 testbit( EV_REL, evt ) ) )
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

		if ( iev.type != EV_KEY && iev.type != EV_REL)
			continue;

		switch ( iev.code )
		{
			case BTN_LEFT:		i = 0; break;
			case BTN_MIDDLE:	i = 1; break;
			case BTN_RIGHT:		i = 2; break;
			case REL_WHEEL:      i = 3; break;
				break;
			default:
				continue;
				break;
		}

		snd_seq_ev_clear( &ev );

		switch ( ev.type = map[i].ev_type )
		{
			case SND_SEQ_EVENT_CONTROLLER:

				snd_seq_ev_set_controller( &ev, map[i].channel,
												map[i].number,
												iev.value == DOWN ? 127 : 0 );
				break;

			case SND_SEQ_EVENT_NOTEON:
				
				snd_seq_ev_set_noteon( &ev, map[i].channel,
											map[i].number,
											iev.value == DOWN ? 127 : 0 );
				break;

			default:
				fprintf( stderr,
						 "Internal error: invalid mapping!\n" );
				continue;
				break;
		}

		send_event( &ev );
	}
}
