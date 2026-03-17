file(REMOVE_RECURSE
  "hackrf_usb.elf"
  "hackrf_usb.elf.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang ASM C)
  include(CMakeFiles/hackrf_usb.elf.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
