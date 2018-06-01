/*
 * Copyright (c) 2018, Dennis Fleurbaaij
 * MIT License
 *
 * Shows the state of the machine (IP, CPU temp) and the onewire sensors (Temp of first temp sensor)
 */

#include <glib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h> 
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "ssd1306.h"
#include "font5x7.h"

#define UPDATE_TIME_SEC 1
#define NSEC_PER_SEC    1000000000L 

#define ETHERNET_INTERFACE_NAME "eth0"
#define OWFS_ROOT "/mnt/1wire"

//
// Data gathering
//

// Get IP address of given interface name. Will write max 16 bytes to write_buffer.
void get_ip(char* interface_name, char* write_buffer)
{
	 struct ifreq ifr;
	 ifr.ifr_addr.sa_family = AF_INET;	 
	 strncpy(ifr.ifr_name, interface_name, IFNAMSIZ-1);

	 int fd = socket(AF_INET, SOCK_DGRAM, 0);
	 ioctl(fd, SIOCGIFADDR, &ifr);	 
	 close(fd);

	 sprintf(write_buffer, "%s", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}

// Get entire contents of file into buffer, max of n bytes. Returns amount of bytes read.
size_t get_file_contents(char* file_name, char* buffer, int n) 
{
	size_t bytes_read = 0;
	
	FILE* file = fopen(file_name, "r");
	if (file) {
		bytes_read = fread(buffer, 1, n, file);
		fclose(file);
	}
	
	return bytes_read;
}

// Get CPU temp as double.
double get_cpu_temp()
{
	char buf[10]; // 5 numbers used
	double temp = 0.0;
	
	if (get_file_contents("/sys/devices/virtual/thermal/thermal_zone0/temp", buf, 10) > 0) {
		temp = atoi(buf);
	}
	
	return temp / 1000.0;
}

// Get amount of OWFS sensors
int owfs_sensor_count()
{
	int count = 0;
	
	struct dirent *dir;
	DIR *d = opendir(OWFS_ROOT);
	if (d) {
		while ((dir = readdir(d)) != NULL) {
			
			if (dir->d_type != DT_DIR)
				continue;
			
			if (strlen(dir->d_name) != 15)
				continue;
			
			if (dir->d_name[2] != '.')
				continue;
			
			++count;
		}
		closedir(d);
    }
	
	return count;	
}

// Get temp of first found OWFS temp sensor at index. Returns > 0 if found.
int get_first_owfs_temp(char* sensor_name, double* temp)
{
	char file_name_buf[256];
	int found = 0;
	
	struct dirent *dir;
	DIR *d = opendir(OWFS_ROOT);
	if (d) {
		while ((dir = readdir(d)) != NULL) {
			
			if (dir-> d_type != DT_DIR)
				continue;
			
			if (strncmp(dir->d_name, "28.", 3) == 0) {
				
				memcpy(sensor_name, dir->d_name, strlen(dir->d_name));
				sensor_name[strlen(dir->d_name)] = 0;
				
				snprintf(file_name_buf, sizeof(file_name_buf), "%s/%s/temperature", OWFS_ROOT, dir->d_name);
				
				char buf[10];
				if(get_file_contents(file_name_buf, buf, sizeof(buf)) > 0) {
					*temp = atof(buf);
					found = 1;
				}
			}
		}
		closedir(d);
    }
	
	return found;
}

//
// Display functions
// 

// Write a single char to the display using the current font
int write_char(struct display_info* disp, int offset, char c)
{
	memcpy(disp->buffer + offset, &font[c * 5], 5);
	return offset + 5;
}

// Write a string of chars to the display using the current font. Assumes \0 terminated.
int write_str(struct display_info* disp, int offset, char* s)
{
	while(*s != 0) {
		offset = write_char(disp, offset, *s);
		++s;
	}
	
	return offset;
}

int write_formatted(struct display_info* disp, int offset, char* buf, char* fmt, ...) 
{
	va_list va;
    va_start(va, fmt);
    vsprintf(buf, fmt, va);
    va_end (va);
}

// Main loop
int ssd1306_machine_state(struct display_info* disp)
{
	struct timespec t;
	int status = 0, cycle = 0, step = 0, offset;
	char line_buffer[32]; // Too big, don't care
	struct sized_array payload;

	while (1) {
		
		// Set the next timestamp
		clock_gettime(CLOCK_MONOTONIC ,&t);
		t.tv_sec += UPDATE_TIME_SEC;

		payload.size = sizeof(display_draw);
		payload.array = display_draw;

		status = ssd1306_send(disp, &payload);
		if (status < 0)
			return -1;

		// Build buffer to send
		memset(disp->buffer, 0, sizeof(disp->buffer));
		offset = 0;
		
		write_str(disp, 0, "   === 1wire server === ");
		
		char ip_str[16];
		get_ip(ETHERNET_INTERFACE_NAME, &ip_str[0]);
		offset = write_str(disp, 256, "IP  : ");
		offset = write_str(disp, offset, &ip_str[0]);
		
		snprintf(line_buffer, sizeof(line_buffer), "Temp: %.1f", get_cpu_temp());
		write_str(disp, 384, line_buffer);
		
		snprintf(line_buffer, sizeof(line_buffer), "OWFS: %i sensors", owfs_sensor_count());
		write_str(disp, 512, line_buffer);
		
		char sensor_name[32];
		double owfs_temp;
		if(get_first_owfs_temp(&sensor_name[0], &owfs_temp) > 0) {
			snprintf(line_buffer, sizeof(line_buffer), " %s: %.1f", sensor_name, owfs_temp);
			write_str(disp, 640, line_buffer);
		}
		
		// Flush buffer
		status = ssd1306_send_buffer(disp);
		if (status < 0)
			return status;

		cycle++;
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
	}

	return 0;
}

void show_error(void)
{
	const gchar* errmsg;
	errmsg = g_strerror(errno);
	printf("\nERROR: %s\n\n", errmsg);
}

void show_usage(char* progname)
{
	printf("\nUsage:\n%s <I2C bus device node>\n", progname);
}

int main(int argc, char** argv)
{
	char filename[32];
	const gchar* errmsg;
	struct display_info disp;

	if (argc < 2) {
		show_usage(argv[0]);
		return -1;
	}

	memset(&disp, 0, sizeof(disp));

	sprintf(filename, argv[1]);
	disp.address = SSD1306_I2C_ADDR;

	if (ssd1306_open(&disp, filename) < 0 ||
	    ssd1306_init(&disp)           < 0 ||
	    ssd1306_machine_state(&disp)  < 0)
	{
		show_error();
	}

	return 0;
}
