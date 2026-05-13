@echo off
REM Run this from the ROOT of your project (where server.c is)
REM Requires: MSYS2 with mingw64, libcurl, cJSON installed

cd /d "%~dp0"
cd ..

gcc -Wall ^
  functions/main.c ^
  functions/seismic.c ^
  functions/meteorological.c ^
  functions/hydro.c ^
  functions/predictor.c ^
  functions/dsp.c ^
  Data/globals.c ^
  Data/data_parser.c ^
  Data/data_ingestor.c ^
  input_handler.c ^
  server.c ^
  -I headers ^
  -I functions ^
  -I Data ^
  -lcjson -lcurl -lws2_32 -lm ^
  -o main.exe

echo Done. Run main.exe
