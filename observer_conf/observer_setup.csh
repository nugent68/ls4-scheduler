#!/usr/bin/tcsh
#
set LS4_SRC = /home/ls4/testing
source ~/.tcshrc
set SIM_FLAG = 1

cd $LS4_ROOT
if ( ! -e quest-src-lasilla ) then
  ln -s $LS4_SRC/quest-src-lasilla .
endif

cd $LS4_ROOT
if ( ! -e questlib ) then
  ln -s ./quest-src-lasilla/questlib .
endif

cd $LS4_ROOT
if ( ! -e scheduler ) then
  ln -s $LS4_SRC/ls4-scheduler ./scheduler
endif

cd $LS4_ROOT
if ( ! -e .login ) ln -s ./scheduler/observer_conf/observer_dot_login ~/.login

cd $LS4_ROOT
if ( ! -e ls4_control ) then
  if ( $SIM_FLAG ) then
    ln -s $LS4_SRC/ls4_control_sim ./ls4_control
  else
    ln -s $LS4_SRC/ls4_control ./ls4_control
  endif
endif

cd $LS4_ROOT
if  ( ! -e bin) then
  mkdir bin
  cd bin
  ln -s $LS4_SRC/quest-src-lasilla/bin/* .
  ln -s $LS4_SRC/ls4-scheduler/scripts/* .
  ln -s $LS4_SRC/ls4-scheduler/bin .
endif

cd $LS4_ROOT
if ( ! -e logs ) mkdir logs
if ( ! -e obsplans ) mkdir obsplans


