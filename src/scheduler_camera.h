/* scheduler_camera.h

   Camera constants used by scheduler_camera.c

   DLR 2025 May 15

*/


// socket connections to ls4 camera controller (ls4_control)
#define MACHINE_NAME "pco-nuc"
#define COMMAND_PORT 5000  
#define STATUS_PORT 5001  


#define COMMAND_DELAY_USEC 100000 /* useconds to wait between commands */

/* first word in reply from camera controller */
#define ERROR_REPLY "ERROR"
#define DONE_REPLY "DONE"



#define CLEAR_TIME 20 /* seconds required to clear camera  */
// DEBUG
#define NUM_CAMERA_CLEARS 0 /* number of clears per camera clear */
//#define NUM_CAMERA_CLEARS 2 /* number of clears per camera clear */
#define READOUT_TIME_SEC 40
#define TRANSFER_TIME_SEC 10
#define EXPOSURE_OVERHEAD ((READOUT_TIME_SEC + 5.0)/3600.0) /* hours */

/* Timeout after 10 seconds if expecting quick response from
   a camera command */

#define CAMERA_TIMEOUT_SEC 5 

/* LS4 exposure modes  (see "ls4_control/archon-main/archon/ls4/ls4_exp_modes.py")*/
#define EXP_MODE_SINGLE "single"
#define EXP_MODE_FIRST "first"
#define EXP_MODE_NEXT "next"
#define EXP_MODE_LAST "last"


/* Camera  Commands  (see "ls4_control/archon-main/archon/ls4/ls4_commands.py")

        'open_shutter':{'arg_name_list':[],'comment':'open the camera shutter'},\
        'close_shutter':{'arg_name_list':[],'comment':'close the camera shutter'},\
        'status':{'arg_name_list':[],'comment':'return camera status'},\
        'expose':{'arg_name_list':['shutter','exptime','fileroot','exp_mode'],\
            'comment':'expose for exptime sec with shutter open (True,False) using exp_mode and saved to fileroot'},\
        'clear':{'arg_name_list':['clear_time'],'comment':'clear the camera for specified time'},\
        'erase':{'arg_name_list':[],'comment':'execute camera erase procedure'},\
        'purge':{'arg_name_list':['fast'],'comment':'run purge procedure with fast option (True,False)'},\
        'flush':{'arg_name_list':['fast','flushcount'],'comment':'execute flush procedure with fast option (True,Flase) and flushcount iterations (int)'},\
        'clean':{'arg_name_list':['erase','n_cycles','flushcount','fast'],'comment':'execute clean procedure with optional erase (True,False) for n_cycle interations (int) followed by a flush with fast (True/False) and flushcount(int) options'},\
        'vsub_on':{'arg_name_list':[],'comment':'enable Vsub bias. Bias power must be on already (see power_on)'},\
        'vsub_off':{'arg_name_list':[],'comment':'disable Vsub bias. Bias power must be on already (see power_on)'},\
        'power_on':{'arg_name_list':[],'comment':'turn on enabled bias voltages.'},\
        'power_off':{'arg_name_list':[],'comment':'turn off bias voltages. '},\
        'autoclear_on':{'arg_name_list':[],'comment':'turn on autoclearing when CCDs are idle.'},\
        'autoclear_off':{'arg_name_list':[],'comment':'turn off autoclearing.'},\
        'autoflush_on':{'arg_name_list':[],'comment':'turn on autoflushing when CCDs are idle.'},\
        'autoflush_off':{'arg_name_list':[],'comment':'turn off autoflushing.'},\
        'header':{'arg_name_list':['key_word','value'],\
             'comment':'add keyword/value to image fits header'},\
        'help':{'arg_name_list':[],'comment':'list commands and their args'}\
*
*/

// camera commands used by scheduler:
#define OPEN_COMMAND "open_shutter"
#define CLOSE_COMMAND "close_shutter"
#define STATUS_COMMAND "status"
#define CLEAR_COMMAND "clear"
#define HEADER_COMMAND "header" /* h keyword value */
#define EXPOSE_COMMAND "expose" /* shutter exptime fileroot */
#define SHUTDOWN_COMMAND "shutdown"
#define REBOOT_COMMAND "reboot"
#define RESTART_COMMAND "restart"



