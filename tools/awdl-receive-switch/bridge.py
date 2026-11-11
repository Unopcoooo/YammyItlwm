"""Root-owned local bridge; native preference edges, existing bounded kernel lease."""
from pathlib import Path
import json,os,signal,subprocess,time,hashlib,sys
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(BASE))
from policy import Policy, RECEIVE_SECONDS
STATE=Path('/var/db/org.unopco.awdl-receive-switch')
UUID='D6ED6749-6BCD-332E-871C-667166716DEE'
ADMISSION_ATTEMPTS=31
ADMISSION_RETRY_SECONDS=.5
stop=False
mono=lambda:time.clock_gettime(time.CLOCK_MONOTONIC)

def run(args, timeout=5, check=True):
    q=subprocess.run(args,capture_output=True,text=True,timeout=timeout,env={'PATH':'/usr/bin:/bin:/usr/sbin:/sbin','LANG':'en_US.UTF-8'})
    if check and q.returncode:raise RuntimeError(f'{Path(args[0]).name} {args[1:]}: {q.returncode} {q.stderr[:200]}')
    return q

def probe(*args, query=False):
    q=run([str(BASE/'native-search-probe'),*map(str,args)])
    return json.loads(q.stdout) if query else q.stdout

def mode():
    uid=os.stat('/dev/console').st_uid
    if uid<501:return None,None
    try:
        q=run(['/bin/launchctl','asuser',str(uid),str(BASE/'native-airdrop-mode')],timeout=2)
        value={0:'Off',1:'Contacts Only',2:'Everyone'}.get(int(q.stdout.strip()))
    except (RuntimeError,ValueError,subprocess.TimeoutExpired):
        return uid,None
    return uid,value

def record(event,**data):
    row=dict(at=time.time(),event=event,**data)
    # Bound disk use; root-owned directory, no user-controlled paths.
    log=STATE/'events.jsonl'
    if log.exists() and log.stat().st_size>2*1024*1024:log.replace(STATE/'events.previous.jsonl')
    with log.open('a') as f:f.write(json.dumps(row)+'\n')
    tmp=STATE/'state.tmp';tmp.write_text(json.dumps(row,indent=2)+'\n');tmp.replace(STATE/'state.json')

def verify():
    assert os.geteuid()==0
    for name,expected in json.loads((BASE/'hashes.json').read_text()).items():
        p=BASE/name;s=p.lstat();assert p.is_file() and not p.is_symlink() and s.st_uid==0 and not s.st_mode&0o022
        assert hashlib.sha256(p.read_bytes()).hexdigest()==expected,name
    loaded=run(['/usr/bin/kmutil','showloaded','--list-only'],timeout=15).stdout
    assert UUID in loaded,'unreviewed driver; no automatic start'

def main():
    global stop
    verify();STATE.mkdir(mode=0o700,exist_ok=True)
    assert not STATE.is_symlink() and STATE.stat().st_uid==0 and not STATE.stat().st_mode&0o077
    p=Policy();browser=None;browser_end=0;console=None
    signal.signal(signal.SIGTERM,lambda *_:globals().__setitem__('stop',True))
    signal.signal(signal.SIGINT,lambda *_:globals().__setitem__('stop',True))
    record('monitor-started',startup_does_not_arm=True,driver_uuid=UUID)
    try:
        while not stop:
            uid,current=mode()
            if console is not None and uid!=console:
                if p.epoch is not None:probe('--native-stop',p.epoch)
                p=Policy();record('console-changed-stopped')
            console=uid
            # Read kernel status only for an owned session or a possible preference edge.
            edge=p.previous is not None and p.previous!='Everyone' and current=='Everyone'
            status=probe('--status',query=True) if p.epoch is not None or edge else {}
            old=p.previous
            actions=p.step(current,status,mono())
            if current!=old:record('preference',mode=current,uid=uid)
            for a in actions:
                record('action',action=a)
                if a=='start':
                    verify() # Pin the reviewed ABI at every admission, too.
                    started=False
                    for attempt in range(ADMISSION_ATTEMPTS):
                        try:
                            probe('--receive-start')
                            started=True
                            break
                        except RuntimeError as exc:
                            admission=probe('--admission',query=True)
                            record('admission-refused',attempt=attempt,error=str(exc),admission=admission)
                            flags=admission.get('FirmwareFlags',0)
                            retryable=admission.get('Reason')==15 or (
                                admission.get('Reason')==11 and flags&(4|512))
                            if not retryable or attempt+1==ADMISSION_ATTEMPTS:
                                record('admission-exhausted',attempts=attempt+1,admission=admission)
                                break
                            time.sleep(ADMISSION_RETRY_SECONDS)
                            next_uid,next_mode=mode()
                            if next_uid!=console or next_mode!='Everyone':
                                record('admission-wait-cancelled',uid=next_uid,mode=next_mode)
                                break
                    if not started:continue
                    accepted=probe('--status',query=True);p.accepted(accepted,mono())
                    record('lease-started',epoch=p.epoch,kernel_deadline_ns=accepted['DeadlineNs'],seconds=RECEIVE_SECONDS)
                elif isinstance(a,tuple):
                    probe('--native-stop' if a[0]=='stop' else '--control-trial',a[1])
                elif a=='retired':
                    record('lease-retired',search=status,control=probe('--control-trial-status',query=True),requires_new_off_to_everyone_edge=True)
            if p.epoch is not None and p.control and not p.stopping and not p.browsed:
                pm=probe('--native-control-state',query=True);trial=probe('--control-trial-status',query=True)
                if trial.get('State')==4:raise RuntimeError('control quarantined')
                if p.browse(pm,trial):
                    browser=subprocess.Popen([str(BASE/'native-service-browse'),'--browse'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                    browser_end=p.deadline;record('bounded-native-browse',epoch=p.epoch)
            if browser is not None and (mono()>=browser_end or p.stopping or p.epoch is None):
                if browser.poll() is None:browser.terminate()
                browser.wait(timeout=3);browser=None
            time.sleep(.5)
    except Exception as exc:
        record('bridge-error',error=str(exc),epoch=p.epoch)
        raise
    finally:
        if browser is not None:
            if browser.poll() is None:browser.terminate()
            browser.wait(timeout=3)
        if p.epoch is not None:
            try:probe('--native-stop',p.epoch);record('shutdown-stop-requested',epoch=p.epoch)
            except Exception as exc:record('shutdown-stop-error',error=str(exc),kernel_deadline_remains_bounded=True)

if __name__=='__main__':main()
