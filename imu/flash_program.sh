#!/bin/bash

picotool load build/digit_recognition.uf2
picotool load build/letter_recognition.bin --offset 0x10140000