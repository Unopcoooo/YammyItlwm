"""Preference edges authorize one bounded lease. No I/O or radio operations here."""
RECEIVE_SECONDS = 300


class Policy:
    def __init__(self):
        self.previous = None
        self.epoch = None
        self.deadline = 0
        self.control = False
        self.browsed = False
        self.stopping = False
        self.pending_edge = False

    def step(self, mode, status, now):
        actions = []
        known = mode in ('Everyone', 'Contacts Only', 'Off')
        edge = known and self.previous is not None and self.previous != 'Everyone' and mode == 'Everyone'
        if edge and self.epoch is not None:
            # A user edge during retirement is a new explicit request. Carry it
            # across the bounded drain instead of silently consuming it.
            self.pending_edge = True
        elif mode != 'Everyone':
            self.pending_edge = False
        if known:
            self.previous = mode
        else:
            # Unknown preferences break edge continuity; recovery is a baseline, not authorization.
            self.previous = None
        if self.epoch is not None:
            if status.get('Epoch') != self.epoch:
                # Never cancel an epoch owned by another caller.
                self.epoch = None
                self.pending_edge = False
                return ['ownership-lost']
            if status.get('WorkerPending') == 0:
                restart = self.pending_edge and mode == 'Everyone'
                self.epoch = None
                self.pending_edge = False
                return ['retired', 'start'] if restart and status.get('WorkerPending', 0) == 0 else ['retired']
            if not known or mode != 'Everyone' or now >= self.deadline:
                if not self.stopping:
                    self.stopping = True
                    actions.append(('stop', self.epoch))
            elif not self.stopping and status.get('BackendEnabled') == 1 and not self.control:
                self.control = True
                actions.append(('control', self.epoch))
        elif edge:
            # A busy/faulted driver rejects admission itself. Never adopt its lease.
            if status.get('WorkerPending', 0) == 0:
                self.pending_edge = False
                actions.append('start')
            else:
                actions.append('busy-edge-consumed')
        return actions

    def accepted(self, status, now):
        epoch = status.get('Epoch')
        assert type(epoch) is int and epoch > 0 and status.get('WorkerPending') == 1
        assert 0 < status['DeadlineNs'] - status['StartNs'] <= RECEIVE_SECONDS * 1000000000
        self.epoch, self.deadline = epoch, now + RECEIVE_SECONDS
        self.control = self.browsed = self.stopping = False
        self.pending_edge = False

    def browse(self, pm, trial):
        if self.epoch is None or self.stopping or self.browsed or not self.control:
            return False
        ready = (pm.get('SearchEpoch') == self.epoch and pm.get('ConfiguredKnown') == 1
                 and pm.get('Configured') == 1 and pm.get('ConfigureInProgress') == 0
                 and pm.get('VirtualLinkState') == 2 and pm.get('BackendEnabled') == 1
                 and pm.get('NativeRoleAccepted') == 1 and pm.get('NativeRoleTxConfirmed') == 1
                 and trial.get('Epoch') == self.epoch and trial.get('Retired') != 1)
        if ready:
            self.browsed = True
        return ready
