/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

#if defined(__OpenBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/namei.h>
#include <sys/sensors.h>
#include <ddb/db_var.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <machine/cpu.h>

#ifdef CPU_BIOS
#include <machine/biosvar.h>
#endif

/* Maximum size object to expect from sysctl(2) */
#define SYSCTL_BUFSIZ	8192

struct ctlname topname[] = CTL_NAMES;
struct ctlname kernname[] = CTL_KERN_NAMES;
struct ctlname vmname[] = CTL_VM_NAMES;
struct ctlname fsname[] = CTL_FS_NAMES;
struct ctlname netname[] = CTL_NET_NAMES;
struct ctlname hwname[] = CTL_HW_NAMES;
struct ctlname tcname[] = CTL_KERN_TIMECOUNTER_NAMES;

#ifdef CTL_MACHDEP_NAMES
struct ctlname machdepname[] = CTL_MACHDEP_NAMES;
#endif

struct ctlname ddbname[] = CTL_DDB_NAMES;

char names[BUFSIZ];
char *equ = "=";

struct list {
	struct	ctlname *list;
	int	size;
};

struct list toplist = { topname, CTL_MAXID };
struct list secondlevel[] = {
	{ 0, 0 },			/* CTL_UNSPEC */
	{ kernname, KERN_MAXID },	/* CTL_KERN */
	{ vmname, VM_MAXID },		/* CTL_VM */
	{ fsname, FS_MAXID },		/* CTL_FS */
	{ netname, NET_MAXID },		/* CTL_NET */
	{ 0, CTL_DEBUG_MAXID },		/* CTL_DEBUG */
	{ hwname, HW_MAXID },		/* CTL_HW */

#ifdef CTL_MACHDEP_NAMES
	{ machdepname, CPU_MAXID },	/* CTL_MACHDEP */
#else
	{ 0, 0 },			/* CTL_MACHDEP */
#endif
	{ 0, 0 },			/* was CTL_USER */
	{ ddbname, DBCTL_MAXID },	/* CTL_DDB_NAMES */
	{ 0, 0 },			/* CTL_VFS */
};

int	Aflag, aflag, nflag, qflag;

time_t boottime;

/*
* Variables requiring special processing.
*/
#define	BIOSGEO		0x00000040
#define	UNSIGNED	0x00000200
#define	SENSORS		0x00002000
#define	SMALLBUF	0x00004000
#define	HEX			0x00008000

// Prototypes
int findname(char *, char *, char **, struct list *);
int sysctl_sensors(char *, char **, int *, int, int *);
void print_sensordev(char *, int *, u_int, struct sensordev *);
void print_sensor(struct sensor *);
char openbsd_temp[10];

int
parse_hex_char(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');

	ch = tolower((unsigned char)ch);
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 10);

	return (-1);
}

ssize_t
parse_hex_string(unsigned char *dst, size_t dstlen, const char *src)
{
	ssize_t len = 0;
	int digit;

	while (len < dstlen) {
		if (*src == '\0')
			return (len);

		digit = parse_hex_char(*src++);
		if (digit == -1)
			return (-1);
		dst[len] = digit << 4;

		digit = parse_hex_char(*src++);
		if (digit == -1)
			return (-1);
		
		dst[len] |= digit;
		len++;
	}

	while (*src != '\0') {
		if (parse_hex_char(*src++) == -1 ||
		    parse_hex_char(*src++) == -1)
			return (-1);

		len++;
	}

	return (len);
}

/*
 * Print sensors from the specified device.
 */

void
print_sensordev(char *string, int mib[], u_int mlen, struct sensordev *snsrdev)
{
	char buf[SYSCTL_BUFSIZ];
	enum sensor_type type;

	if (mlen == 3) {
		for (type = 0; type < SENSOR_MAX_TYPES; type++) {
			mib[3] = type;
			snprintf(buf, sizeof(buf), "%s.%s",
			    string, sensor_type_s[type]);
			print_sensordev(buf, mib, mlen+1, snsrdev);
		}
		return;
	}

	if (mlen == 4) {
		int numt;

		type = mib[3];
		for (numt = 0; numt < snsrdev->maxnumt[type]; numt++) {
			mib[4] = numt;
			snprintf(buf, sizeof(buf), "%s%u", string, numt);
			print_sensordev(buf, mib, mlen+1, snsrdev);
		}
		return;
	}

	if (mlen == 5) {
		struct sensor snsr;
		size_t slen = sizeof(snsr);

		/* this function is only printing sensors in bulk, so we
		 * do not return any error messages if the requested sensor
		 * is not found by sysctl(3)
		 */
		if (sysctl(mib, 5, &snsr, &slen, NULL, 0) == -1)
			return;

		if (slen > 0 && (snsr.flags & SENSOR_FINVALID) == 0) {
			if (!nflag)
				printf("%s%s", string, equ);
			print_sensor(&snsr);
			//printf("\n");
		}
		return;
	}
}

void
print_sensor(struct sensor *s)
{
	const char *name;

	if (s->flags & SENSOR_FUNKNOWN)
		printf("unknown");
	else {
		switch (s->type) {
		case SENSOR_TEMP:
		snprintf(openbsd_temp, sizeof(  openbsd_temp), "%.2f", (s->value - 273150000) / 1000000.0);

			break;
		/*case SENSOR_FANRPM:
			printf("%lld RPM", s->value);
			break;
		case SENSOR_FREQ:
			printf("%.2f Hz", s->value / 1000000.0);
			break;
		case SENSOR_ENERGY:
			printf("%.2f J", s->value / 1000000.0);
			break;*/
		default:
			printf("unknown");
		}
	}

	switch (s->status) {
	case SENSOR_S_UNSPEC:
		break;
	case SENSOR_S_OK:
		printf(", OK");
		break;
	case SENSOR_S_WARN:
		printf(", WARNING");
		break;
	case SENSOR_S_CRIT:
		printf(", CRITICAL");
		break;
	case SENSOR_S_UNKNOWN:
		printf(", UNKNOWN");
		break;
	}

	if (s->tv.tv_sec) {
		time_t t = s->tv.tv_sec;
		char ct[26];

		ctime_r(&t, ct);
		ct[19] = '\0';
		printf(", %s.%03ld", ct, s->tv.tv_usec / 1000);
	}
}

void
parse(char *string, int flags)
{
	int indx, type, state, intval, len;
	size_t size, newsize = 0;
	int lal = 0, special = 0;
	void *newval = NULL;
	int64_t quadval;
	struct list *lp;
	int mib[CTL_MAXNAME];
	char *cp, *bufp, buf[SYSCTL_BUFSIZ];
	unsigned char hex[SYSCTL_BUFSIZ];

	(void)strlcpy(buf, string, sizeof(buf));

	bufp = buf;
	if ((cp = strchr(string, '=')) != NULL) {
		*strchr(buf, '=') = '\0';
		*cp++ = '\0';
		while (isspace((unsigned char)*cp))
			cp++;
		newval = cp;
		newsize = strlen(cp);
	}
	if ((indx = findname(string, "top", &bufp, &toplist)) == -1)
		return;

	mib[0] = indx;
	lp = &secondlevel[indx];

	if (lp->list == 0) {
		warnx("%s: class is not implemented", topname[indx].ctl_name);
		return;
	}

	if ((indx = findname(string, "second", &bufp, lp)) == -1)
		return;
	mib[1] = indx;
	type = lp->list[indx].ctl_type;
	len = 2;
	switch (mib[0]) {
	case CTL_HW:
		switch (mib[1]) {
		case HW_SENSORS:
			special |= SENSORS;
			len = sysctl_sensors(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
	
			break;
		}
		break;

	default:
		warnx("illegal top level value: %d", mib[0]);
		return;

	}

	if (bufp) {
		warnx("name %s in %s is unknown", bufp, string);
		return;
	}

	if (newsize > 0) {
		const char *errstr;

		switch (type) {
		case CTLTYPE_INT:
			if (special & UNSIGNED)
				intval = strtonum(newval, 0, UINT_MAX, &errstr);
			else
				intval = strtonum(newval, INT_MIN, INT_MAX,
				    &errstr);
			if (errstr != NULL) {
				warnx("%s: value is %s: %s", string, errstr,
				    (char *)newval);
				return;
			}
			newval = &intval;
			newsize = sizeof(intval);
			break;

		case CTLTYPE_QUAD:
			(void)sscanf(newval, "%lld", &quadval);
			newval = &quadval;
			newsize = sizeof(quadval);
			break;
		case CTLTYPE_STRING:
			if (special & HEX) {
				ssize_t len;

				len = parse_hex_string(hex, sizeof(hex),
				    newval);
				if (len == -1) {
					warnx("%s: hex string %s: invalid",
					    string, (char *)newval);
					return;
				}
				if (len > sizeof(hex)) {
					warnx("%s: hex string %s: too long",
					    string, (char *)newval);
					return;
				}

				newval = hex;
				newsize = len;
			}
			break;
		}
	}

	size = (special & SMALLBUF) ? 512 : SYSCTL_BUFSIZ;
	if (sysctl(mib, len, buf, &size, newval, newsize) == -1) {
		if (flags == 0)
			return;
		switch (errno) {
		case EOPNOTSUPP:
			warnx("%s: value is not available", string);
			return;
		case ENOTDIR:
			warnx("%s: specification is incomplete", string);
			return;
		case ENOMEM:
			warnx("%s: type is unknown to this program", string);
			return;
		case ENXIO:
			if (special & BIOSGEO)
				return;
		default:
			warn("%s", string);
			return;
		}
	}

	switch (type) {
	case CTLTYPE_INT:
		if (newsize == 0) {
			if (!nflag)
				(void)printf("%s%s", string, equ);
			if (special & HEX)
				(void)printf("0x%x\n", *(int *)buf);
			else
				(void)printf("%d\n", *(int *)buf);
		} else {
			if (!qflag) {
				if (!nflag)
					(void)printf("%s: %d -> ", string,
					    *(int *)buf);
				if (special & HEX)
					(void)printf("0x%x\n", *(int *)newval);
				else
					(void)printf("%d\n", *(int *)newval);
			}
		}
		return;

	case CTLTYPE_STRING:
		if (newval == NULL) {
			if (!nflag)
				(void)printf("%s%s", string, equ);
			if (special & HEX) {
				size_t i;
				for (i = 0; i < size; i++) {
					(void)printf("%02x",
					    (unsigned char)buf[i]);
				}
				(void)printf("\n");
			} else
				(void)puts(buf);
		} else if (!qflag) {
			if (!nflag) {
				(void)printf("%s: ", string);
				if (special & HEX) {
					size_t i;
					for (i = 0; i < size; i++) {
						(void)printf("%02x",
						    (unsigned char)buf[i]);
					}
				} else
					(void)printf("%s", buf);

				(void)printf(" -> ");
			}
			(void)puts(cp);
		}
		return;

	case CTLTYPE_QUAD:
		if (newsize == 0) {
			int64_t tmp;

			memcpy(&tmp, buf, sizeof tmp);
			if (!nflag)
				(void)printf("%s%s", string, equ);
			(void)printf("%lld\n", tmp);
		} else {
			int64_t tmp;

			memcpy(&tmp, buf, sizeof tmp);
			if (!qflag) {
				if (!nflag)
					(void)printf("%s: %lld -> ",
					    string, tmp);
				memcpy(&tmp, newval, sizeof tmp);
				(void)printf("%lld\n", tmp);
			}
		}
		return;

	case CTLTYPE_STRUCT:
		warnx("%s: unknown structure returned", string);
		return;

	default:
	case CTLTYPE_NODE:
		warnx("%s: unknown type returned", string);
		return;
	}	
}

/*
 * Handle hardware monitoring sensors support
 */
int
sysctl_sensors(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	char *devname, *typename;
	int dev, numt, i;
	enum sensor_type type;
	struct sensordev snsrdev;
	size_t sdlen = sizeof(snsrdev);

	/*
	 * If we get this far, it means that some arguments were
	 * provided below hw.sensors tree.
	 * The first branch of hw.sensors tree is the device name.
	 */
	if ((devname = strsep(bufpp, ".")) == NULL) {
		warnx("%s: incomplete specification", string);
		return (-1);
	}
	/* convert sensor device string to an integer */
	for (dev = 0; ; dev++) {
		mib[2] = dev;
		if (sysctl(mib, 3, &snsrdev, &sdlen, NULL, 0) == -1) {
			if (errno == ENXIO)
				continue;
			if (errno == ENOENT)
				break;
			warn("sensors dev %d", dev);
			return (-1);
		}
		if (strcmp(devname, snsrdev.xname) == 0)
			break;
	}
	if (strcmp(devname, snsrdev.xname) != 0) {
		warnx("%s: sensor device not found: %s", string, devname);
		return (-1);
	}
	if (*bufpp == NULL) {
		/* only device name was provided -- let's print all sensors
		 * that are attached to the specified device
		 */
		print_sensordev(string, mib, 3, &snsrdev);
		return (-1);
	}

	/*
	 * At this point we have identified the sensor device,
	 * now let's go further and identify sensor type.
	 */
	if ((typename = strsep(bufpp, ".")) == NULL) {
		warnx("%s: incomplete specification", string);
		return (-1);
	}
	numt = -1;
	for (i = 0; typename[i] != '\0'; i++)
		if (isdigit((unsigned char)typename[i])) {
			const char *errstr;

			numt = strtonum(&typename[i], 0, INT_MAX, &errstr);
			if (errstr) {
				warnx("%s: %s", string, errstr);
				return (-1);
			}
			typename[i] = '\0';
			break;
		}
	for (type = 0; type < SENSOR_MAX_TYPES; type++)
		if (strcmp(typename, sensor_type_s[type]) == 0)
			break;
	if (type == SENSOR_MAX_TYPES) {
		warnx("%s: sensor type not recognised: %s", string, typename);
		return (-1);
	}
	mib[3] = type;

	/*
	 * If no integer was provided after sensor_type, let's
	 * print all sensors of the specified type.
	 */
	if (numt == -1) {
		print_sensordev(string, mib, 4, &snsrdev);
		return (-1);
	}

	/*
	 * At this point we know that we have received a direct request
	 * via command-line for a specific sensor. Let's have the parse()
	 * function deal with it further, and report any errors if such
	 * sensor node does not exist.
	 */
	mib[4] = numt;
	*typep = CTLTYPE_STRUCT;
	return (5);
}

/*
 * Scan a list of names searching for a particular name.
 */
int 
findname(char *string, char *level, char **bufp, struct list *namelist)
{
	char *name;
	int i;

	if (namelist->list == 0 || (name = strsep(bufp, ".")) == NULL) {
		warnx("%s: incomplete specification", string);
		return (-1);
	}
	for (i = 0; i < namelist->size; i++)
		if (namelist->list[i].ctl_name != NULL &&
		    strcmp(name, namelist->list[i].ctl_name) == 0)
			break;
	if (i == namelist->size) {
		warnx("%s level name %s in %s is invalid", level, name, string);
		return (-1);
	}
	return (i);
}

void
get_sensor_temp()
{
  nflag = 1;
  
  ctime(&boottime); /* satisfy potential $TZ expansion before unveil() */

  /*if (unveil(_PATH_DEVDB, "r") == -1 && errno != ENOENT)
	err(1,"unveil %s", _PATH_DEVDB);
  if (unveil("/dev", "r") == -1 && errno != ENOENT)
	err(1, "unveil /dev");
  if (unveil(NULL, NULL) == -1)
	err(1, "unveil");*/

  char *sensor = "hw.sensors.ksmn0.temp";

  parse(sensor, 1);
  return;
}
#endif //__OpenBSD__

#if defined(__FreeBSD__)
  # include <sys/types.h>
  # include <sys/sysctl.h>
#endif

#include "bspwmbar.h"
#include "util.h"

void
thermal(draw_context_t *dc, module_option_t *opts)
{
#if defined(__linux)
	static time_t prevtime;
	static unsigned long temp;
	static int thermal_found = -1;

	if (thermal_found == -1) {
		if (access(opts->thermal.sensor, F_OK) != -1)
			thermal_found = 1;
		else
			thermal_found = 0;
	}
	if (!thermal_found)
		return;

	time_t curtime = time(NULL);
	if (curtime - prevtime < 1)
		goto DRAW_THERMAL;
	prevtime = curtime;

	if (pscanf(opts->thermal.sensor, "%ju", &temp) == -1)
		return;

DRAW_THERMAL:
	if (!opts->thermal.prefix)
		opts->thermal.prefix = "";
	if (!opts->thermal.suffix)
		opts->thermal.suffix = "";

	sprintf(buf, "%s%lu%s", opts->thermal.prefix, temp / 1000,
	        opts->thermal.suffix);
#elif defined(__OpenBSD__)
	//int mib[3] = { HW_SENSORS, 0 };
	//sprintf(buf, "%sNOIMPL%s", opts->thermal.prefix, opts->thermal.suffix);
	get_sensor_temp();
	sprintf(buf, "%s%s%s", opts->thermal.prefix, openbsd_temp,
	        opts->thermal.suffix);
	
#elif defined(__FreeBSD__)
	int temp;
	size_t templen = sizeof(temp);

	char ctlname[64] = { 0 };
	sprintf(ctlname, "hw.acpi.thermal.%s.temperature", opts->thermal.sensor);
	if (sysctlbyname(ctlname, &temp, &templen, NULL, 0) < 0) {
		return;
	}

	double atemp = (double)temp / 10 - 273.15;
	sprintf(buf, "%s%.*f%s", opts->thermal.prefix, 1, atemp,
	        opts->thermal.suffix);
#endif

	draw_text(dc, buf);
}
