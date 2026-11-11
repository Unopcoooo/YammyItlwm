// Local bounded AWDL control client. --self-test does not open an IOKit service.
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

static BOOL parseEpoch(const char *text,uint64_t *out){
 if(!text||!*text||!out)return NO;uint64_t value=0;
 for(const char *p=text;*p;++p){if(*p<'0'||*p>'9')return NO;unsigned digit=*p-'0';if(value>(UINT64_MAX-digit)/10)return NO;value=value*10+digit;}
 if(!value)return NO;*out=value;return YES;
}
static NSDictionary *request(unsigned command,uint64_t epoch){
 if(command>9||((command==0||command==3||command==6||command==7||command==8)?epoch==0:epoch!=0))return nil;
 return @{@"AWDLNativeSearchCommand":@{@"Version":@1,@"Command":@(command),@"Epoch":@(epoch)}};
}
static BOOL schemaValid(id v){
 return [v isKindOfClass:NSNumber.class]&&CFGetTypeID((__bridge CFTypeRef)v)!=CFBooleanGetTypeID()&&
  !CFNumberIsFloatType((__bridge CFNumberRef)v)&&[v unsignedLongLongValue]==1;
}
int main(int argc,const char **argv){@autoreleasepool{
 if(argc==2&&!strcmp(argv[1],"--self-test")){
  // Native stop is now the host-search stop command, not SYNC_ENABLED=false.
  assert(request(0,1)&&!request(0,0)&&request(9,0)&&!request(9,1)&&!request(10,0));
  assert([request(0,1)[@"AWDLNativeSearchCommand"] count]==3);
  uint64_t epoch=0;assert(parseEpoch("1",&epoch)&&epoch==1);assert(parseEpoch("18446744073709551615",&epoch)&&epoch==UINT64_MAX);
  for(NSString *bad in @[@"",@"0",@"-1",@"+1",@" 1",@"1x",@"18446744073709551616"])assert(!parseEpoch(bad.UTF8String,&epoch));
  assert(schemaValid(@1)&&!schemaValid(@YES)&&!schemaValid(@1.0)&&!schemaValid(@0)&&!schemaValid(@"1")&&!schemaValid(nil));
  puts("PASS native stop maps to command 0 with a live nonzero epoch; no registry or RF");return 0;
 }
 uint64_t epoch=0;
 BOOL receive=argc==2&&!strcmp(argv[1],"--receive-start");
 BOOL cycle=argc==3&&!strcmp(argv[1],"--control-cycle")&&parseEpoch(argv[2],&epoch);
 BOOL tx=argc==3&&!strcmp(argv[1],"--control-tx")&&parseEpoch(argv[2],&epoch),txStatus=argc==2&&!strcmp(argv[1],"--control-tx-status");
 BOOL status=argc==2&&!strcmp(argv[1],"--status"),admission=argc==2&&!strcmp(argv[1],"--admission"),start=argc==2&&!strcmp(argv[1],"--start"),stop=argc==3&&!strcmp(argv[1],"--stop")&&parseEpoch(argv[2],&epoch);
 BOOL trial=argc==3&&!strcmp(argv[1],"--control-trial")&&parseEpoch(argv[2],&epoch),trialStatus=argc==2&&!strcmp(argv[1],"--control-trial-status");
 BOOL readiness=argc==2&&!strcmp(argv[1],"--native-control-state");
 BOOL nativeStart=argc==2&&!strcmp(argv[1],"--native-start"),nativeStop=argc==3&&!strcmp(argv[1],"--native-stop")&&parseEpoch(argv[2],&epoch),nativeQuery=argc==2&&!strcmp(argv[1],"--native-query");
 if(!receive&&!cycle&&!tx&&!txStatus&&!status&&!admission&&!start&&!stop&&!nativeStart&&!nativeStop&&!nativeQuery&&!readiness&&!trial&&!trialStatus){fprintf(stderr,"usage: --self-test | --status | --admission | --start | --stop EPOCH | --native-start | --native-stop EPOCH | --native-query | --native-control-state | --control-trial EPOCH | --control-trial-status | --control-tx EPOCH | --control-tx-status | --control-cycle EPOCH\n");return 2;}
 if(!txStatus&&!status&&!admission&&!trialStatus&&geteuid()){fprintf(stderr,"start/stop requires root\n");return 1;}
 io_iterator_t it=0;if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("AirportItlwm"),&it))return 1;
 io_service_t service=IOIteratorNext(it),extra=IOIteratorNext(it);IOObjectRelease(it);
 if(!service||extra){if(service)IOObjectRelease(service);if(extra)IOObjectRelease(extra);fprintf(stderr,"expected exactly one controller\n");return 1;}
 CFTypeRef schema=IORegistryEntryCreateCFProperty(service,CFSTR("AWDLNativeSearchSchema"),kCFAllocatorDefault,0);
 BOOL valid=schemaValid((__bridge id)schema);if(schema)CFRelease(schema);
 if(!valid){IOObjectRelease(service);fprintf(stderr,"loaded driver has no bounded search schema; nothing submitted\n");return 1;}
 if(receive||cycle||tx||txStatus||nativeStart||nativeStop||nativeQuery||readiness||trial||trialStatus){
  CFTypeRef cap=IORegistryEntryCreateCFProperty(service,receive?CFSTR("AWDLNativeReceiveTrialSchema"):cycle?CFSTR("AWDLNativeControlCycleSchema"):(tx||txStatus)?CFSTR("AWDLNativeControlTxSchema"):(trial||trialStatus)?CFSTR("AWDLNativeControlTrialSchema"):(readiness?CFSTR("AWDLNativeControlStateSchema"):CFSTR("AWDLNativeSyncProbeSchema")),kCFAllocatorDefault,0);
  BOOL available=schemaValid((__bridge id)cap);if(cap)CFRelease(cap);
  if(!available){IOObjectRelease(service);fprintf(stderr,"%s; nothing submitted\n",(tx||txStatus)?"loaded driver has no control-TX schema":(trial||trialStatus)?"loaded driver has no native control-trial schema":(readiness?"loaded driver has no native control-state schema":"loaded driver has no native sync probe schema"));return 1;}
 }
 if(txStatus||status||admission||trialStatus){
  CFTypeRef property=IORegistryEntryCreateCFProperty(service,txStatus?CFSTR("AWDLNativeControlTx"):trialStatus?CFSTR("AWDLNativeControlTrial"):(admission?CFSTR("AWDLNativeSearchAdmission"):CFSTR("AWDLNativeSearch")),kCFAllocatorDefault,0);IOObjectRelease(service);
  id value=property?(__bridge id)property:@{@"Schema":@1,@"Published":@NO};
  NSData *json=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingPrettyPrinted error:nil];
  if(json){fwrite(json.bytes,1,json.length,stdout);putchar('\n');}if(property)CFRelease(property);return json?0:1;
 }
 // Command 0 is the versioned host-search stop. The old helper used command 3,
 // which only disabled the native SYNC request and no longer owned the lease.
 IOReturn result=IORegistryEntrySetCFProperties(service,(__bridge CFTypeRef)request(receive?9:cycle?8:tx?7:trial?6:(readiness?5:(nativeStart?2:(nativeStop?0:(nativeQuery?4:(start?1:0))))),epoch));
 if(readiness&&result==kIOReturnSuccess){
  CFTypeRef property=IORegistryEntryCreateCFProperty(service,CFSTR("AWDLNativeControlState"),kCFAllocatorDefault,0);IOObjectRelease(service);
  NSData *json=property?[NSJSONSerialization dataWithJSONObject:(__bridge id)property options:NSJSONWritingPrettyPrinted error:nil]:nil;
  if(json){fwrite(json.bytes,1,json.length,stdout);putchar('\n');}if(property)CFRelease(property);return json?0:1;
 }
 IOObjectRelease(service);
 printf("driver command result=0x%08x; native stop requests the host-search teardown\n",result);return result==kIOReturnSuccess?0:1;
}}
