# LoRaMesher
This library aims to implement a user firendly way to create a mesh network between multiple ESP32 LoRa Nodes. This library is currently under construction.

## Dependencies
You can check `library.json` for more details. Basically we use a modded version of [Radiolib](https://github.com/jgromes/RadioLib) that supports class methods as callbacks and [FreeRTOS](https://freertos.org/index.html) for scheduling maintenance tasks

### Code Quality
We are using [Astyle](http://astyle.sourceforge.net/) to make the code look the same. You need to run the following command after the first `git clone` in order to set the apropiate code checks:
```bash
git config  core.hookspath .githooks #For newer versions of git
ln -s ../../.githooks/pre-commit.sh .git/hooks/pre-commit #For older versions of git
```
