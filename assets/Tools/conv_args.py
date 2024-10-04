#!/bin/python3

# Copyright (c) 2023 Rumbledethumps
# SPDX-License-Identifier: MIT-0
# SPDX-License-Identifier: Unlicense

from PIL import Image

import sys

inputfile = sys.argv[1]
outputfile = sys.argv[2]

print ('Input file is ', inputfile)
print ('Output file is ', outputfile)

def main(argv):
   inputfile = ''
   outputfile = ''
   opts, args = getopt.getopt(argv,"hi:o:",["ifile=","ofile="])
   for opt, arg in opts:
      if opt == '-h':
         print ('test.py -i <inputfile> -o <outputfile>')
         sys.exit()
      elif opt in ("-i", "--ifile"):
         inputfile = arg
      elif opt in ("-o", "--ofile"):
         outputfile = arg
   print ('Input file is ', inputfile)
   print ('Output file is ', outputfile)

def rp6502_rgb(r, g, b):
    if r == 0 and g == 0 and b == 0:
        return 0
    else:
        return (((b >> 3) << 11) | ((g >> 3) << 6) | (r >> 3)) | 1 << 5


def conv_img(name_in, name_out):
    with Image.open(name_in) as im:
        im2 = im.convert("P", palette=Image.ADAPTIVE, colors=256)
        with open("bin/" + name_out + ".dat.bin", "wb") as o:
            for y in range(0, int(im2.height / 2)):
                for x in range(0, int(im2.width / 2)):
                    o.write(
                        im2.getpixel((x * 2, y * 2)).to_bytes(
                            1, byteorder="little", signed=False
                        )
                    )
        with open("bin/" + name_out + ".pal.bin", "wb") as o:
            for r, g, b in zip(*[iter(im2.getpalette())] * 3):
                o.write(
                    rp6502_rgb(r, g, b).to_bytes(2, byteorder="little", signed=False)
                )


def conv_spr(name_in, name_out):
    with Image.open(name_in) as im:
        with open("bin/" + name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[16, 16])
            for y in range(0, im2.height):
                for x in range(0, im2.width):
                    r, g, b = im2.getpixel((x, y))
                    o.write(
                        rp6502_rgb(r, g, b).to_bytes(
                            2, byteorder="little", signed=False
                        )
                    )

# conv_img("samuelcust/sprites/background-day.png", "bg_day_144x256")
print ('Input file is ', "samuelcust/sprites/" + inputfile)
print ('Output file is ', outputfile)
conv_spr("samuelcust/sprites/" + inputfile, outputfile)
