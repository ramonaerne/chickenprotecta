#!/bin/bash
sed -E 's/(char (auth|ssid|pass)\[] = )".*";/\1"****";/g;s/(LOC_LONGITUDE |LOC_LATITUDE ).*/\140.0/g' chickenprotecta/chickenprotecta.ino > chickenprotecta-public.ino
git add chickenprotecta-public.ino 
