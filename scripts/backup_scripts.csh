#!/bin/csh
#
# for each file in scheduler/scripts, check if version exists in ~/palomar/scripts, ~/bin/ or ~. If so, copy it to ~/scheduler/scripts
#
#
set TEST = 0
#
cd ~/scheduler/scripts
#
foreach file ( `ls * ` )
  echo "#checking $file"
  if ( -e ~/palomar/scripts/$file ) then
      echo "#cp ~/palomar/scripts/$file ."
      set l = `diff ~/palomar/scripts/$file $file`
      if ( $#l == 0 ) then
         echo "# ~/palomar/scripts/$file $file no difference"
      else
         echo "# ~/palomar/scripts/$file $file differences:"
         diff ~/palomar/scripts/$file $file
      endif
      if ( $TEST == 0 ) then
           cp ~/palomar/scripts/$file .
      endif
  else if ( -e ~/bin/$file ) then
     echo "#cp ~/bin/$file ."
      set l = `diff ~/bin/$file $file`
      if ( $#l == 0 ) then
         echo "# ~/bin/$file $file no difference"
      else
         echo "# ~/bin/$file $file differences:"
         diff ~/bin/$file $file
      endif
     if ( $TEST == 0 ) then
           cp ~/bin/$file .
     endif
  else if ( -e ~/$file ) then
     echo "#cp ~/$file ."
      set l = `diff ~/$file $file`
      if ( $#l == 0 ) then
         echo "# ~/bin/$file $file no difference"
      else
         echo "# ~/$file $file differences:"
         diff ~/$file $file
      endif
     if ( $TEST == 0 ) then 
         cp ~/$file .
     endif
  else
     echo "# can't find $file"
  endif
end
