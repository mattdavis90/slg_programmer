#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <i2c/smbus.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define NVM_CONFIG    0x02
#define EEPROM_CONFIG 0x03

uint8_t i2c_slave_addr = 0x01;

void print_usage(const char* a);
bool read_slg(int i2c_bus, bool target_nvm);
bool write_slg(int i2c_bus, const char* filename, bool target_nvm);
bool erase_slg(int i2c_bus, bool target_nvm);

int main(int argc, char* argv[])
{
    bool erase      = false;
    bool read       = false;
    bool write      = false;
    bool target_nvm = true;
    char* filename;
    int c;
    while ((c = getopt(argc, argv, ":erw:t:i:")) != -1) {
        switch (c) {
            case 'e': {
                erase = true;
                break;
            }
            case 'r': {
                read = true;
                break;
            }
            case 'w': {
                write    = true;
                filename = optarg;
                break;
            }
            case 't': {
                if (strcasecmp(optarg, "nvm") == 0) {
                    target_nvm = true;
                } else if (strcasecmp(optarg, "eeprom") == 0) {
                    target_nvm = false;
                } else {
                    printf("Error: target must be one of NVM or EEPROM\n");
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            }
            case 'i': {
                i2c_slave_addr = atoi(optarg);
                if (i2c_slave_addr < 0 || i2c_slave_addr > 15) {
                    printf("Error: id must be in the range 0-15\n");
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            }
            case ':': {
                printf("Error: -%c without filename\n", optopt);
                print_usage(argv[0]);
                return 1;
            }
            case '?': {
                printf("Error: Unknown arg %c\n", optopt);
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    if ((argc - optind) < 1) {
        printf("Error: Missing i2c bus\n");
        print_usage(argv[0]);
        return 1;
    }

    int i2c_num = atoi(argv[optind]);
    char i2c_name[16];
    snprintf(i2c_name, 15, "/dev/i2c-%d", i2c_num);
    int i2c_bus = open(i2c_name, O_RDWR);
    if (i2c_bus < 0) {
        printf("Error: Failed to open i2c bus\n");
        return 1;
    }

    bool ok = false;
    if (read) {
        ok = read_slg(i2c_bus, target_nvm);
    } else if (write) {
        ok = erase_slg(i2c_bus, target_nvm);
        if (ok) {
            printf("Waiting for powercycle\n");
            getchar();
            ok = write_slg(i2c_bus, filename, target_nvm);
            if (ok) {
                printf("Waiting for powercycle\n");
                getchar();
                ok = read_slg(i2c_bus, target_nvm);
            }
        }
    } else if (erase) {
        ok = erase_slg(i2c_bus, target_nvm);
    }
    if (!ok)
        return 1;

    return 0;
}

void print_usage(const char* a)
{
    printf("Usage: %s [-i <id>] [-t nvm|eeprom] [-e] [-r] [-w <filename>] <i2c> \n", a);
}

bool read_slg(int i2c_bus, bool target_nvm)
{
    printf("Starting read\n");

    uint8_t control_code
        = (i2c_slave_addr << 3) | (target_nvm ? NVM_CONFIG : EEPROM_CONFIG);

    for (int page = 0; page < 16; page++) {
        uint8_t i2c_cmd[] = { page << 4 };
        uint8_t i2c_page_data[16];

        struct i2c_msg i2c_msgs[] = {
            {
                .addr  = control_code,
                .flags = 0,
                .buf   = i2c_cmd,
                .len   = sizeof(i2c_cmd),
            },
            {
                .addr  = control_code,
                .flags = I2C_M_RD,
                .buf   = i2c_page_data,
                .len   = sizeof(i2c_page_data),
            },
        };

        struct i2c_rdwr_ioctl_data ioctl_data = {
            .msgs  = i2c_msgs,
            .nmsgs = sizeof(i2c_msgs) / sizeof(struct i2c_msg),
        };

        int ret = ioctl(i2c_bus, I2C_RDWR, &ioctl_data);
        if (ret < 0) {
            printf("Error: ioctl returned %d. Reason: %s (errno=%d)\n", ret,
                strerror(errno), errno);
            return false;
        }

        printf("%02X:  ", page << 4);
        for (int i = 0; i < 16; i++) {
            printf("%02X ", i2c_page_data[i]);
        }
        printf("\n");
    }

    return true;
}

bool write_slg(int i2c_bus, const char* filename, bool target_nvm)
{
    printf("Reading HEX file\n");

    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        printf("Error: Could not open hex file\n");
        return false;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        printf("Error: fstat failed\n");
        return false;
    }

    char* buf = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED) {
        printf("Error: mmap failed\n");
        return false;
    }

    uint8_t pages[16][16];
    for (int page = 0; page < 16; page++) {
        for (int offset = 0; offset < 16; offset++) {
            size_t ptr = (page * 44) + 9 + (2 * offset);
            char tmp[3];
            snprintf(tmp, 3, "%c%c", buf[ptr], buf[ptr + 1]);

            uint8_t b           = strtol(tmp, NULL, 16);
            pages[page][offset] = b;
        }
    }

    munmap(buf, sb.st_size);
    close(fd);

    printf("Starting write\n");

    uint8_t control_code
        = (i2c_slave_addr << 3) | (target_nvm ? NVM_CONFIG : EEPROM_CONFIG);
    for (int page = 0; page < 16; page++) {
        uint8_t buf[17] = { 0 };

        buf[0] = page << 4;
        for (int i = 0; i < 16; i++) {
            buf[i + 1] = pages[page][i];
        }

        struct i2c_msg i2c_msgs[] = {
            {
                .addr  = control_code,
                .flags = 0,
                .buf   = buf,
                .len   = sizeof(buf),
            },
        };

        struct i2c_rdwr_ioctl_data ioctl_data = {
            .msgs  = i2c_msgs,
            .nmsgs = sizeof(i2c_msgs) / sizeof(struct i2c_msg),
        };

        int ret = ioctl(i2c_bus, I2C_RDWR, &ioctl_data);
        if (ret < 0) {
            printf("Error: ioctl returned %d. Reason: %s (errno=%d)\n", ret,
                strerror(errno), errno);
            return false;
        }

        printf("%02X:  ", page << 4);
        for (int offset = 0; offset < 16; offset++) {
            printf("%02X ", pages[page][offset]);
        }
        printf("\n");

        usleep(20000); // 20ms
    }

    return true;
}

bool erase_slg(int i2c_bus, bool target_nvm)
{
    printf("Starting erase\n");

    uint8_t control_code = i2c_slave_addr << 3;

    for (int page = 0; page < 16; page++) {
        printf("Erasing %02X: ", page << 4);

        uint8_t i2c_cmd[] = { 0xE3, (target_nvm ? 0x80 : 0x90) | page };

        struct i2c_msg i2c_msgs[] = {
            {
                .addr  = control_code,
                .flags = 0,
                .buf   = i2c_cmd,
                .len   = sizeof(i2c_cmd),
            },
        };

        struct i2c_rdwr_ioctl_data ioctl_data = {
            .msgs  = i2c_msgs,
            .nmsgs = sizeof(i2c_msgs) / sizeof(struct i2c_msg),
        };

        int ret = ioctl(i2c_bus, I2C_RDWR, &ioctl_data);
        if (ret < 0) {
            printf("Error: ioctl returned %d. Reason: %s (errno=%d)\n", ret,
                strerror(errno), errno);
            return false;
        }
        usleep(20000); // 20ms

        printf("Ok\n");
    }

    return true;
}
