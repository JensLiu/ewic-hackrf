file(REMOVE_RECURSE
  "hackrf_usb_ram.elf"
  "hackrf_usb_ram.elf.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang ASM C)
  include(CMakeFiles/hackrf_usb_ram.elf.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
