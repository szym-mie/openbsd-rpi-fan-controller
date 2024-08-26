#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sensors.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/gpio.h>

struct config
{
	float trig_run;
	float trig_100;
	int cycle_us;
	int duty_idle;
	int duty_run;
	int duty_100;
	int pin_number;
	int pin_invert;
};

#define PWM_PIN 26

static int gpio_fd;

void 
gpio_out(unsigned int pin, int lvl)
{
	struct gpio_pin_op gpio_op;
	gpio_op.gp_name[0] = '\0';
	gpio_op.gp_pin = pin;
	gpio_op.gp_value = lvl;
	ioctl(gpio_fd, GPIOPINWRITE, &gpio_op);
}

int
sensordev_same(struct sensordev *sdev, char name[16])
{
	for (int si = 0; si < 16; si++)
	{
		if (sdev->xname[si] != name[si]) break;
		if (sdev->xname[si] == '\0') return 1;
	}
	
	return 0;
}

int
sensor_find(char name[16], enum sensor_type type, struct sensor *sns)
{
	struct sensordev sdev;
	size_t sdevl = sizeof(sdev);
	size_t snsl = sizeof(*sns);

	int mib[] = { CTL_HW, HW_SENSORS, 0, type, 0 };
	int ndx;

	for (ndx = 0;; ndx++)
	{
		mib[2] = ndx;
		if (sysctl(mib, 3, &sdev, &sdevl, NULL, 0) == -1)
		{
			if (errno == ENXIO) continue;
			if (errno == ENOENT) return 1;
			return 2;
		}
		printf("sdev %s %u (%u)\n", sdev.xname, sdev.num, sdev.sensors_count);
		if (sensordev_same(&sdev, name)) break;
	}

	printf("found sdev\n");

	for (ndx = 0; ndx < sdev.sensors_count; ndx++)
	{
		mib[4] = ndx;
		if (sysctl(mib, 5, sns, &snsl, NULL, 0) == -1)
		{
			perror("sensor err");			
		}
		printf("s %s %lld\n", sns->desc, sns->value);
	}

	return 0;
}

int
temp_get(char sns_name[16], float *t)
{
	struct sensor ts;
	if (sensor_find(sns_name, SENSOR_TEMP, &ts)) return 1;
	
	*t = (float)(ts.value - 273150000) / 1000000.0;
	return 0;
}
	
int should_run = 1;

void
on_exit(int signo)
{
	gpio_out(PWM_PIN, 1);
	close(gpio_fd);
	should_run = 0;
}

int
main(void)
{
	gpio_fd = open("/dev/gpio0", O_RDWR);
	if (gpio_fd == -1) return 1;

	signal(SIGINT, on_exit);
	signal(SIGTERM, on_exit);	

	int period = 200;
	int duty_time = 200;
	int free_time = 0;

	int max_time = 4000;
	int run_time = max_time * 5;

	float temp = 0.0;
	float temp_ovth;

	while (should_run)
	{
		free_time = period - duty_time >= 0 
			? period - duty_time 
			: 0;

		usleep(free_time);
		if (duty_time > 0) gpio_out(PWM_PIN, 0);
		usleep(duty_time);
		if (duty_time < period) gpio_out(PWM_PIN, 1);
		run_time -= period;
		if (run_time < 0)
		{
			run_time = max_time;
			temp_get("bcmtmon0", &temp);
			temp_ovth = temp - 40.0;
			duty_time = temp_ovth > 0 ? 80 + (int)(temp_ovth * 24.0) : 20;
			printf("t = %f %u/%u\n", temp, duty_time, period);
		}
	}

	return 0;
}
