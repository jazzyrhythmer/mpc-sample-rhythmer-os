#!/usr/bin/env python3
"""Refresh guide images that have matching UI preview states in this runtime."""
from pathlib import Path
import shutil,sys
source,target=map(Path,sys.argv[1:3])
updated=matched=0
for image in source.glob('*.png'):
 destination=target/image.name
 if not destination.is_file():continue
 matched+=1
 if destination.read_bytes()!=image.read_bytes():
  shutil.copy2(image,destination);updated+=1
print(f'Guide screens: {matched} matched states, {updated} refreshed from this runtime')
