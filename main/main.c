#include <stdio.h>
#include "vending_machine.h"




void app_main(void)
{

    vm_init();
    display_gui_init();
    display_gui_update_lines("aqui se escribe lo de la Línea 1", "pero  aqui viene lo de la Línea 2");
    

}


