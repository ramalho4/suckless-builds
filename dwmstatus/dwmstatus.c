/*
 * Copy me if you can.
 * by 20h
 */
#include <ctype.h>

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

char *tzargentina = "America/Buenos_Aires";
char *tzutc = "UTC";
char *tzberlin = "Europe/Berlin";
char *tzdenver = "America/Denver";
static Display *dpy;



char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}



char *volume_status(void) {
    static char buf[32];
    FILE *fp;
    int vol;
    char mute[8];

    // Get volume
    fp = popen("pactl get-sink-volume @DEFAULT_SINK@ | awk 'NR==1{gsub(\"%\", \"\"); print $5}'", "r");
    if (!fp) return strdup("vol?");
    if(fgets(buf, sizeof(buf), fp) == NULL) {
        pclose(fp);
        return strdup("vol?");
    }
    pclose(fp);

    vol = atoi(buf);

    // Check if muted
    fp = popen("pactl get-sink-mute @DEFAULT_SINK@ | awk '{print $2}'", "r");
    if (!fp) return smprintf("%02d%%", vol);
    if(fgets(mute, sizeof(mute), fp) == NULL) {
        pclose(fp);
        return smprintf("%02d%%", vol);
    }
    pclose(fp);

    if (strncmp(mute, "yes", 3) == 0)
        return smprintf("M:%02d%%", vol);
    else
        return smprintf("%02d%%", vol);
}


char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL) {
		fclose(fd);
		return NULL;
	}
	fclose(fd);

	return smprintf("%s", line);
}

char *getbattery(char *base)
{
    char *co, status;
    int descap, remcap;
    int perc;

    descap = -1;
    remcap = -1;

    co = readfile(base, "present");
    if (co == NULL)
        return smprintf("");
    if (co[0] != '1') {
        free(co);
        return smprintf("not present");
    }
    free(co);

    co = readfile(base, "charge_full_design");
    if (co == NULL) {
        co = readfile(base, "energy_full_design");
        if (co == NULL)
            return smprintf("");
    }
    sscanf(co, "%d", &descap);
    free(co);

    co = readfile(base, "charge_now");
    if (co == NULL) {
        co = readfile(base, "energy_now");
        if (co == NULL)
            return smprintf("");
    }
    sscanf(co, "%d", &remcap);
    free(co);

    co = readfile(base, "status");
    if (!strncmp(co, "Discharging", 11)) {
        status = '-';
    } else if (!strncmp(co, "Charging", 8)) {
        status = '+';
    } else {
        status = '?';
    }

    if (remcap < 0 || descap < 0)
        return smprintf("invalid");

    perc = (int)(((float)remcap / (float)descap) * 100);

    // Zero-padded battery percentage with % and status sign
    return smprintf("%02d%%%c", perc, status);
}


char *brightness_status(void) {
    static char buf[32];
    FILE *fp;
    int bright, max;

    // Read current brightness
    fp = popen("cat /sys/class/backlight/*/brightness", "r");
    if (!fp) return strdup("??%");
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        pclose(fp);
        return strdup("??%");
    }
    pclose(fp);
    bright = atoi(buf);

    // Read max brightness
    fp = popen("cat /sys/class/backlight/*/max_brightness", "r");
    if (!fp) return smprintf("%02d%%", bright);
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        pclose(fp);
        return smprintf("%02d%%", bright);
    }
    pclose(fp);
    max = atoi(buf);

    // Compute percentage
    int perc = (bright * 100) / max;

    return smprintf("%02d%%", perc);
}


char *
gettemperature(char *base, char *sensor)
{
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", atof(co) / 1000);
}

char *
execscript(char *cmd)
{
	FILE *fp;
	char retval[1025], *rv;

	memset(retval, 0, sizeof(retval));

	fp = popen(cmd, "r");
	if (fp == NULL)
		return smprintf("");

	rv = fgets(retval, sizeof(retval), fp);
	pclose(fp);
	if (rv == NULL)
		return smprintf("");
	retval[strlen(retval)-1] = '\0';

	return smprintf("%s", retval);
}

char *wifi_status(void) {
	static char buf[128];
	FILE *fp;
	
	fp = popen("nmcli -t -f ACTIVE,SSID dev wifi | grep '^yes:' | cut -d: -f2", "r");
	if (!fp) return strdup("no wifi");
	
	if(fgets(buf,sizeof(buf), fp) == NULL) {
		pclose(fp);
		return strdup("disconnected");
		
	} 
	pclose(fp);
	
	buf[strcspn(buf, "\n")] = 0;
	return strdup(buf);	
}
#include <unistd.h>
int main(void)
{
    char *status;
    char *bat = NULL;
    char *wifi = NULL;
    char *vol;
    char *tmar;
    int counter = 0;
	char *bright;
    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "dwmstatus: cannot open display.\n");
        return 1;
    }

    // Initial values for slow-changing items
    bat = getbattery("/sys/class/power_supply/BAT0");
    wifi = wifi_status();

    for (;;) {
        // Update date/time every loop
        tmar = mktimes("%A, %B %-d %I:%M %p", tzdenver);
     	int vol_int = atoi(volume_status());  // convert string to int
    	int bat_int = atoi(bat);   
        // Fast-changing item: volume (and brightness if desired)
        vol = volume_status();
        bright = brightness_status(); 

        
            bat = getbattery("/sys/class/power_supply/BAT0");
	    bat_int = atoi(bat);
            
            wifi = wifi_status();
        

        // Construct and set status bar
        status = smprintf(" [[%s] [bat:%s] [vol:%s] [lcd:%s] [%s]", wifi, bat, vol, bright, tmar);
        setstatus(status);
       


        // Free temporary strings
	free(bat);
	free(wifi);
        free(vol);
        free(bright); // uncomment if you add brightness
        free(tmar);
        free(status);

        counter++;
        usleep(10000); // 0.1s delay → near-instant updates
    }

    // Free remaining memory
    free(bat);
    free(wifi);
    XCloseDisplay(dpy);

    return 0;
}

