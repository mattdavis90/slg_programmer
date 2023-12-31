# SLG Programmer

An I2C programmer for the Renesas SLG product line. *Note: Only tested on an SLG46826*

## Usage

```bash
./slg_programmer [-i <id>] [-t nvm|eeprom] [-e] [-r] [-w <filename>] <i2c>
```

#### Arguments

* `i2c` - This is the number of the i2c bus. You can find this using `i2cdetect -l`

#### Options

* `-i <id>` - The base I2C address for the SLG. 0 after an erase or defaults to 1
* `-t nvm|eeprom` - Act on the NVM or the EEPROM
* `-e` - Erase the selected target
* `-r` - Read the selected target
* `-w <filename>` - Write the contents of the Intel Hex `filename` to the target

## Building

Building should be as simple as running GNU Make - make sure you have libi2c installed (`libi2c-devel` on Fedora)

```bash
make
```

or, if you don't have Make installed then

```bash
gcc -li2c -o slg_programmer main.c
```