build:
	cd code && idf.py build

flash:
	cd code && idf.py flash

monitor:
	cd code && idf.py monitor

flash-monitor: flash monitor

clean:
	cd code && idf.py clean

deploy:
	rsync -avz --delete --filter=':- .gitignore' --filter='- .sl/' --filter='- .git/' . digink.lan:./node/

full-cycle: clean build flash monitor

.PHONY: build flash monitor flash-monitor clean deploy full-cycle
