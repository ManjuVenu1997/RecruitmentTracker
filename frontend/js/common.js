// ─────────────────────────────────────────────
//  common.js – shared utilities for all pages
// ─────────────────────────────────────────────

const API = {
  async fetch(path, opts = {}) {
    const token = localStorage.getItem('token');
    const headers = {
      'Content-Type': 'application/json',
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
      ...(opts.headers || {})
    };
    const res = await fetch('/api' + path, { ...opts, headers });
    if (res.status === 401) {
      localStorage.clear();
      window.location.href = '/';
      return null;
    }
    return res;
  },
  get:    (path)          => API.fetch(path),
  post:   (path, body)    => API.fetch(path, { method: 'POST',   body: JSON.stringify(body) }),
  put:    (path, body={}) => API.fetch(path, { method: 'PUT',    body: JSON.stringify(body) }),
  delete: (path)          => API.fetch(path, { method: 'DELETE' })
};

function getStoredUser() {
  try { return JSON.parse(localStorage.getItem('user') || 'null'); } catch { return null; }
}

function requireAuth(allowedRoles) {
  const user = getStoredUser();
  const token = localStorage.getItem('token');
  if (!token || !user) { window.location.href = '/'; return null; }
  if (allowedRoles && !allowedRoles.includes(user.role)) {
    // Redirect to proper dashboard
    if (user.role === 'admin')        window.location.href = '/admin.html';
    else if (user.role === 'recruiter') window.location.href = '/recruiter.html';
    else                               window.location.href = '/interviewer.html';
    return null;
  }
  return user;
}

function logout() {
  API.post('/auth/logout').finally(() => {
    localStorage.clear();
    window.location.href = '/';
  });
}

// ── Date / format helpers ──────────────────────────────
function fmtDt(dt) {
  if (!dt) return '—';
  const d = new Date(dt.includes('T') ? dt : dt.replace(' ', 'T'));
  return d.toLocaleString(undefined, {
    dateStyle: 'medium', timeStyle: 'short'
  });
}
function fmtDate(dt) {
  if (!dt) return '—';
  const d = new Date(dt.includes('T') ? dt : dt.replace(' ', 'T'));
  return d.toLocaleDateString(undefined, { dateStyle: 'medium' });
}
function isoForFC(dt) {
  // SQLite stores without T; FullCalendar needs ISO
  return dt ? dt.replace(' ', 'T') : dt;
}

// ── Toast notifications ────────────────────────────────
function showToast(message, type = 'success') {
  const el = document.createElement('div');
  el.className = `alert alert-${type} shadow position-fixed fade show`;
  el.style.cssText = 'bottom:24px;right:24px;z-index:9999;min-width:280px;max-width:440px;border-radius:10px';
  el.innerHTML = `<i class="bi bi-${type === 'success' ? 'check-circle' : type === 'danger' ? 'x-circle' : 'info-circle'} me-2"></i>${message}`;
  document.body.appendChild(el);
  setTimeout(() => { el.classList.remove('show'); setTimeout(() => el.remove(), 300); }, 3500);
}

// ── Status badge ───────────────────────────────────────
function statusBadge(status) {
  const map = {
    confirmed: ['status-confirmed', 'Confirmed'],
    completed: ['status-completed', 'Completed'],
    declined:  ['status-declined',  'Declined'],
    available: ['status-available', 'Available'],
    blocked:   ['status-blocked',   'Booked'],
  };
  const [cls, label] = map[status] || ['bg-secondary text-white', status];
  return `<span class="badge rounded-pill px-3 ${cls}">${label}</span>`;
}

// ── Notification panel (shared) ────────────────────────
async function initNotifications(userId) {
  const btn   = document.getElementById('notifBtn');
  const panel = document.getElementById('notifPanel');
  const list  = document.getElementById('notifList');
  const badge = document.getElementById('notifBadge');
  if (!btn) return;

  async function load() {
    const res = await API.get('/notifications');
    if (!res) return;
    const data = await res.json();
    const items = data.notifications || [];
    const unread = data.unread_count || 0;

    badge.textContent = unread > 9 ? '9+' : unread;
    badge.classList.toggle('d-none', unread === 0);

    list.innerHTML = items.length === 0
      ? '<div class="text-center text-muted py-4 small">No notifications</div>'
      : items.map(n => `
          <div class="notif-item ${n.is_read ? '' : 'unread'}" data-id="${n.id}">
            <div>${n.message}</div>
            <div class="notif-time">${fmtDt(n.created_at)}</div>
          </div>`).join('');

    list.querySelectorAll('.notif-item').forEach(el => {
      el.addEventListener('click', async () => {
        if (!el.classList.contains('unread')) return;
        await API.put(`/notifications/${el.dataset.id}/read`);
        el.classList.remove('unread');
        const cur = parseInt(badge.textContent) || 0;
        if (cur > 0) { badge.textContent = cur - 1; if (cur - 1 === 0) badge.classList.add('d-none'); }
      });
    });
  }

  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    panel.classList.toggle('d-none');
    if (!panel.classList.contains('d-none')) load();
  });
  document.addEventListener('click', () => panel.classList.add('d-none'));
  panel.addEventListener('click', (e) => e.stopPropagation());

  const markAllBtn = document.getElementById('markAllReadBtn');
  if (markAllBtn) {
    markAllBtn.addEventListener('click', async () => {
      await API.put('/notifications/read-all');
      badge.textContent = '0'; badge.classList.add('d-none');
      load();
    });
  }

  // Poll every 30 s
  load();
  setInterval(load, 30000);
}

// ── Change Password Modal (shared) ───────────────────────
function injectChangePwModal() {
  if (document.getElementById('changePwModal')) return;
  const html = `
  <div class="modal fade" id="changePwModal" tabindex="-1">
    <div class="modal-dialog modal-dialog-centered modal-sm">
      <div class="modal-content border-0 rounded-4">
        <div class="modal-header border-0 pb-0">
          <h5 class="modal-title fw-bold"><i class="bi bi-key me-2 text-warning"></i>Change Password</h5>
          <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
        </div>
        <div class="modal-body">
          <div id="changePwError" class="alert alert-danger d-none"></div>
          <div id="changePwSuccess" class="alert alert-success d-none"></div>
          <div class="mb-3">
            <label class="form-label fw-semibold small">Current Password</label>
            <input type="password" class="form-control" id="currentPw" placeholder="Current password" />
          </div>
          <div class="mb-3">
            <label class="form-label fw-semibold small">New Password</label>
            <input type="password" class="form-control" id="newPw" placeholder="Min 6 characters" />
          </div>
          <div class="mb-1">
            <label class="form-label fw-semibold small">Confirm New Password</label>
            <input type="password" class="form-control" id="confirmPw" placeholder="Repeat new password" />
          </div>
        </div>
        <div class="modal-footer border-0 pt-0">
          <button type="button" class="btn btn-light" data-bs-dismiss="modal">Cancel</button>
          <button type="button" class="btn btn-warning fw-semibold" id="savePwBtn">
            <span id="savePwSpinner" class="spinner-border spinner-border-sm d-none me-1"></span>
            Change Password
          </button>
        </div>
      </div>
    </div>
  </div>`;
  document.body.insertAdjacentHTML('beforeend', html);

  document.getElementById('savePwBtn').addEventListener('click', async () => {
    const cur     = document.getElementById('currentPw').value;
    const nw      = document.getElementById('newPw').value;
    const confirm = document.getElementById('confirmPw').value;
    const errDiv  = document.getElementById('changePwError');
    const okDiv   = document.getElementById('changePwSuccess');
    const spinner = document.getElementById('savePwSpinner');
    const btn     = document.getElementById('savePwBtn');
    errDiv.classList.add('d-none'); okDiv.classList.add('d-none');

    if (!cur)          { errDiv.textContent='Enter your current password'; errDiv.classList.remove('d-none'); return; }
    if (nw.length < 6) { errDiv.textContent='New password must be at least 6 characters'; errDiv.classList.remove('d-none'); return; }
    if (nw !== confirm) { errDiv.textContent='New passwords do not match'; errDiv.classList.remove('d-none'); return; }

    btn.disabled=true; spinner.classList.remove('d-none');
    try {
      const res  = await API.post('/auth/change-password', { current_password: cur, new_password: nw });
      const data = await res.json();
      if (!res.ok) { errDiv.textContent=data.error; errDiv.classList.remove('d-none'); return; }
      okDiv.textContent='Password changed successfully!';
      okDiv.classList.remove('d-none');
      document.getElementById('currentPw').value='';
      document.getElementById('newPw').value='';
      document.getElementById('confirmPw').value='';
      setTimeout(()=>bootstrap.Modal.getInstance(document.getElementById('changePwModal')).hide(), 1500);
    } finally {
      btn.disabled=false; spinner.classList.add('d-none');
    }
  });
}

// ── Navbar init ────────────────────────────────────────
function initNavbar() {
  const user = getStoredUser();
  if (!user) return;
  const el = document.getElementById('navName');
  if (el) el.textContent = user.full_name;
  // Show app version badge
  fetch('/api/version').then(r => r.json()).then(data => {
    const v = document.getElementById('appVersion');
    if (v) v.textContent = 'v' + data.version;
  }).catch(() => {});
  const logoutBtn = document.getElementById('logoutBtn');
  if (logoutBtn) logoutBtn.addEventListener('click', logout);
  initNotifications(user.id);
  injectChangePwModal();
  injectProfileModal();
  // Wire change-password button
  const cpBtn = document.getElementById('changePwBtn');
  if (cpBtn) cpBtn.addEventListener('click', () => {
    document.getElementById('changePwError').classList.add('d-none');
    document.getElementById('changePwSuccess').classList.add('d-none');
    new bootstrap.Modal(document.getElementById('changePwModal')).show();
  });
  // Wire profile button
  const profBtn = document.getElementById('profileBtn');
  if (profBtn) profBtn.addEventListener('click', openProfileModal);

  // ── Auto-refresh session every 15 minutes ─────────
  setInterval(async () => {
    await API.post('/auth/refresh');
  }, 15 * 60 * 1000);
}

// ── Profile Edit Modal (shared) ──────────────────────────
function injectProfileModal() {
  if (document.getElementById('profileModal')) return;
  const html = `
  <div class="modal fade" id="profileModal" tabindex="-1">
    <div class="modal-dialog modal-dialog-centered modal-sm">
      <div class="modal-content border-0 rounded-4">
        <div class="modal-header border-0 pb-0">
          <h5 class="modal-title fw-bold">
            <i class="bi bi-person-circle me-2 text-primary"></i>Edit Profile
          </h5>
          <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
        </div>
        <div class="modal-body">
          <div id="profileError"   class="alert alert-danger  d-none"></div>
          <div id="profileSuccess" class="alert alert-success d-none"></div>
          <div class="mb-3">
            <label class="form-label fw-semibold small">Full Name</label>
            <input type="text" class="form-control" id="profileFullName" maxlength="200" />
          </div>
          <div class="mb-1">
            <label class="form-label fw-semibold small">Email</label>
            <input type="email" class="form-control" id="profileEmail" maxlength="200" />
          </div>
        </div>
        <div class="modal-footer border-0 pt-0">
          <button type="button" class="btn btn-light" data-bs-dismiss="modal">Cancel</button>
          <button type="button" class="btn btn-primary fw-semibold" id="saveProfileBtn">
            <span id="saveProfileSpinner" class="spinner-border spinner-border-sm d-none me-1"></span>
            Save Changes
          </button>
        </div>
      </div>
    </div>
  </div>`;
  document.body.insertAdjacentHTML('beforeend', html);

  document.getElementById('saveProfileBtn').addEventListener('click', async () => {
    const fullName = document.getElementById('profileFullName').value.trim();
    const email    = document.getElementById('profileEmail').value.trim();
    const errDiv   = document.getElementById('profileError');
    const okDiv    = document.getElementById('profileSuccess');
    const spinner  = document.getElementById('saveProfileSpinner');
    const btn      = document.getElementById('saveProfileBtn');
    errDiv.classList.add('d-none'); okDiv.classList.add('d-none');

    if (!fullName) { errDiv.textContent = 'Full name is required'; errDiv.classList.remove('d-none'); return; }
    if (!email)    { errDiv.textContent = 'Email is required';     errDiv.classList.remove('d-none'); return; }

    btn.disabled = true; spinner.classList.remove('d-none');
    try {
      const res  = await API.put('/auth/profile', { full_name: fullName, email });
      const data = await res.json();
      if (!res.ok) { errDiv.textContent = data.error; errDiv.classList.remove('d-none'); return; }
      okDiv.textContent = 'Profile updated!';
      okDiv.classList.remove('d-none');
      // Update stored user
      const stored = getStoredUser();
      if (stored) {
        stored.full_name = fullName;
        stored.email     = email;
        localStorage.setItem('user', JSON.stringify(stored));
        const el = document.getElementById('navName');
        if (el) el.textContent = fullName;
      }
      setTimeout(() => bootstrap.Modal.getInstance(document.getElementById('profileModal')).hide(), 1200);
    } finally {
      btn.disabled = false; spinner.classList.add('d-none');
    }
  });
}

async function openProfileModal() {
  const res  = await API.get('/auth/profile');
  if (!res) return;
  const u = await res.json();
  document.getElementById('profileFullName').value = u.full_name || '';
  document.getElementById('profileEmail').value    = u.email     || '';
  document.getElementById('profileError').classList.add('d-none');
  document.getElementById('profileSuccess').classList.add('d-none');
  new bootstrap.Modal(document.getElementById('profileModal')).show();
}
