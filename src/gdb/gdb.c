/*
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Marcin Rajwa <marcin.rajwa@linux.intel.com>
 *
 * GDB Stub - parse incoming GDB packets, control FW accordingly and provide
 * a reply to the GDB server.
 *
 */

#include <sof/gdb/gdb.h>
#include <sof/gdb/ringbuffer.h>
#include <arch/gdb/xtensa-defs.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <xtensa/xtruntime.h>
#include <sof/interrupt.h>

/* local functions */
static int get_hex(unsigned char ch);
static int hex_to_int(unsigned char **ptr, int *int_value);
static void put_packet(unsigned char *buffer);
static void parse_request(void);
static unsigned char *get_packet(void);
static void gdb_log_exception(char *message);

/* main buffers */
static unsigned char remcom_in_buffer[GDB_BUFMAX];
static unsigned char remcom_out_buffer[GDB_BUFMAX];

static const char hex_chars[] = "0123456789abcdef";

/* registers backup conatiners */
int sregs[256];
int aregs[64];

void gdb_init(void)
{
	init_buffers();
}

/* scan for the GDB packet sequence $<data>#<check_sum> */
unsigned char *get_packet(void)
{
	unsigned char *buffer = &remcom_in_buffer[0];
	unsigned char check_sum;
	unsigned char xmitcsum;
	int count;
	unsigned char ch;

	/* wait around for the start character, ignore all other characters */
	while ((ch = get_debug_char()) != '$')
		;
retry:
	check_sum = 0;
	xmitcsum = -1;
	count = 0;

	/* now, read until a # or end of buffer is found */
	while (count < GDB_BUFMAX - 1) {
		ch = get_debug_char();

		if (ch == '$')
			goto retry;
		if (ch == '#')
			break;
		check_sum = check_sum + ch;
		buffer[count] = ch;
		count++;
	}
	/* mark end of the sequence */
	buffer[count] = 0x00;
	if (ch == '#') {
		/* We have request already, now fetch its check_sum */
		ch = get_debug_char();
		xmitcsum = get_hex(ch) << 4;
		ch = get_debug_char();
		xmitcsum += get_hex(ch);

		if (check_sum != xmitcsum) {
			/* TODO: handle wrong check_sums */
			put_debug_char('+');
		} else {
			/* successful transfer */
			put_debug_char('+');
	/*
	 * if a sequence char is present
	 * reply the sequence ID
	 */
			if (buffer[2] == ':') {
				put_debug_char(buffer[0]);
				put_debug_char(buffer[1]);

				return &buffer[3];
			}
		}
	}
	return buffer;
}

/* Convert ch from a request to an int */
static int get_hex(unsigned char ch)
{
	if (ch >= 'a' && ch <= 'f')
		return ch-'a'+10;
	if (ch >= '0' && ch <= '9')
		return ch-'0';
	if (ch >= 'A' && ch <= 'F')
		return ch-'A'+10;
	return -1;
}

void gdb_handle_exception(void)
{
	gdb_log_exception("Hello from GDB!");
	parse_request();
}

void parse_request(void)
{
	unsigned char *request;
	int addr;

	while (1) {
		request = get_packet();
		/* Log any exception caused by debug exception */
		gdb_debug_info(request);

		/* Pick incoming request handler */
		unsigned char command = *request++;

		switch (command) {
		/* Continue normal program execution and leave debug handler */
		case 'c':
			if (hex_to_int(&request, &addr))
				sregs[DEBUG_PC] = addr;

			/* return from exception */
			return;
		default:
			gdb_log_exception("Unknown GDB command.");
			break;

		}
		/* reply to the request */
		put_packet(remcom_out_buffer);
	}
}

/*
 * While we find nice hex chars, build an int.
 * Return number of chars processed.
 */
static int hex_to_int(unsigned char **ptr, int *int_value)
{
	int num_chars = 0;
	int hex_value;

	*int_value = 0;
	if (NULL == ptr)
		return 0;

	while (**ptr) {
		hex_value = get_hex(**ptr);
		if (hex_value < 0)
			break;

		*int_value = (*int_value << 4) | hex_value;
		num_chars++;
		(*ptr)++;
	}
	return num_chars;
}


/* Send the packet to the buffer */
static void put_packet(unsigned char *buffer)
{
	unsigned char check_sum;
	int count;
	unsigned char ch;

	/* $<packet_info>#<check_sum> */
	do {
		put_debug_char('$');
		check_sum = 0;
		count = 0;

		while ((ch = buffer[count])) {
			put_debug_char(ch);
			buffer[count] = 0;
			check_sum += ch;
			count += 1;
		}

		put_debug_char('#');
		put_debug_char(hex_chars[check_sum >> 4]);
		put_debug_char(hex_chars[check_sum & 0xf]);

	} while (get_debug_char() != '+');
}


static void gdb_log_exception(char *message)
{
	while (*message)
		put_exception_char(*message++);

}
