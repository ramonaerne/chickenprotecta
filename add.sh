#!/bin/bash
sed -E 's/(char .*\[] = )".*";/\1"****";/g' chickenprotecta/chickenprotecta.ino > chickenprotecta-public.ino
git add chickenprotecta-public.ino 
