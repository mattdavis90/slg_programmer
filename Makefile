BINARY = slg_programmer

.PHONY: clean

${BINARY}: main.c
	gcc -li2c -o $@ $<

clean:
	rm -f ${BINARY}
