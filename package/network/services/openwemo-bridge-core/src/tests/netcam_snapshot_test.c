#include <assert.h>
#include <stdio.h>
#include "netcam_motion.h"
int main(void) {
 assert(netcam_motion_parse(NULL,NULL,NULL,-1)==0);
 assert(netcam_motion_parse("1",NULL,NULL,0)==13);
 assert(netcam_motion_normalize(5,1,100,700)==5);
 assert(netcam_motion_normalize(5,1,100,701)==1);
 assert(netcam_motion_normalize(5,0,100,101)==1);
 assert(netcam_motion_normalize(-1,1,100,101)==0);
 assert(!netcam_motion_available(3));assert(!netcam_motion_available(4));
 assert(netcam_motion_normalize(3,1,100,101)==1);assert(netcam_motion_normalize(4,1,100,101)==0);
 assert(netcam_motion_parse("1","future","0",-99)==0);
 assert(netcam_motion_parse("1","motion","1",0)==5);
 assert(netcam_motion_available(5));
 assert(netcam_motion_parse("1","unavailable","0",5)==9);
 assert(!netcam_motion_available(9));
 assert(netcam_motion_parse("0","paused","0",5)==7);
 assert(netcam_motion_parse("0","setup","0",5)==11);
 assert(netcam_motion_parse("0","idle","1",9)==2);
 assert(netcam_motion_parse("1","idle","1",5)==1);
 assert(netcam_motion_parse("1","motion",NULL,5)==1);
 assert(netcam_motion_parse("123456","motion","1",5)==1);
 assert(netcam_motion_parse("1",NULL,NULL,5)==13);
 assert(!netcam_motion_available(13));
 assert(netcam_motion_parse("0","future","1",999)==0);
 puts("PASS: packed snapshot validity, retained occupancy, legacy and malformed states");
}
