/**
\brief General OpenWSN definitions

\author Thomas Watteyne <watteyne@eecs.berkeley.edu>, September 2012
*/

#include "openwsn.h"
//===== drivers
#include "openserial.h"
//===== stack
//-- cross-layer
#include "idmanager.h"
#include "openqueue.h"
#include "openrandom.h"
#include "opentimers.h"
//-- 02a-TSCH
#include "IEEE802154E.h"
//-- 02b-RES
#include "schedule.h"
#include "res.h"
#include "neighbors.h"
#include "processIE.h"
//-- 03a-IPHC
#include "openbridge.h"
#include "iphc.h"
//-- 03b-IPv6
#include "forwarding.h"
#include "icmpv6.h"
#include "icmpv6echo.h"
#include "icmpv6router.h"
#include "icmpv6rpl.h"
//-- 04-TRAN
#include "opentcp.h"
#include "openudp.h"
#include "opencoap.h"
//-- app (common)
//#include "rreg.h"
//#include "rwellknown.h"
//#include "rinfo.h"
//===== applications
//-- TCP
#include "tcpecho.h"
#include "tcpinject.h"
#include "tcpprint.h"
#include "ohlone.h"
//-- UDP
#include "udpecho.h"
#include "udpinject.h"
#include "udpprint.h"
#include "udprand.h"
//#include "udpstorm.h"
//-- CoAP
#include "rleds.h"
#include "rt.h"
#include "rex.h"
//#include "rheli.h"
//#include "rrube.h"
//#include "rxl1.h"
//#include "layerdebug.h"
//-- misc
//#include "heli.h"
//#include "imu.h"

//=========================== variables =======================================

//=========================== prototypes ======================================

void openwsn_init();

//=========================== public ==========================================

//=========================== private =========================================

void openwsn_init() {
   //===== drivers
   openserial_init();
   
   //===== stack
   //-- cross-layer
   idmanager_init();    // call first since initializes e.g. EUI64
   openqueue_init();
   openrandom_init();
   opentimers_init();
   //-- 02a-TSCH
   ieee154e_init();
   //-- 02b-RES
   schedule_init();
   res_init();
   neighbors_init();
   processIE_init();
   //-- 03a-IPHC
   openbridge_init();
   iphc_init();
   //-- 03b-IPv6
   forwarding_init();
   icmpv6_init();
   icmpv6echo_init();
   icmpv6router_init();
   icmpv6rpl_init();
   //-- 04-TRAN
   opentcp_init();
   openudp_init();
   opencoap_init();    // initialize before any of the CoAP applications
   //-- app (common)
   //rreg_init();
   //rwellknown_init();
   //rinfo_init();
   
   //===== applications
   //-- TCP
   tcpecho_init();
   tcpinject_init();
   tcpprint_init();
   ohlone_init();
   //-- UDP
   udpecho_init();
   udpinject_init();
   udpprint_init();
   udprand_init();
   //udpstorm_init();
   //-- CoAP
   //rleds_init();
   //rt_init();
   //rex_init();
   //rheli_init();
   //rrube_init();
   //rxl1_init();
   //layerdebug_init();
   //-- misc
   //heli_init();
   //imu_init();
}