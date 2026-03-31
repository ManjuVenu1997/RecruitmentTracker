// ─────────────────────────────────────────────
//  admin.js
// ─────────────────────────────────────────────
(async () => {
  const user = requireAuth(['admin']);
  if (!user) return;
  initNavbar();

  let calendar;
  let skillChart;
  let currentInterviewId = null;

  // ── Tab navigation ─────────────────────────
  document.querySelectorAll('[data-tab]').forEach(link => {
    link.addEventListener('click', e => {
      e.preventDefault();
      const tab = link.dataset.tab;
      document.querySelectorAll('[id^="tab-"]').forEach(t => t.classList.add('d-none'));
      document.getElementById('tab-' + tab).classList.remove('d-none');
      document.querySelectorAll('[data-tab]').forEach(l => l.classList.remove('active'));
      link.classList.add('active');
      if (tab === 'calendar' && !calendar) initCalendar();
      else if (tab === 'calendar' && calendar) calendar.render();
      else if (tab === 'users') loadUsers();
      else if (tab === 'email-config') loadSmtpConfig();
    });
  });

  // ── User search (debounced) ────────────────
  const searchInput = document.getElementById('userSearchInput');
  const searchClear = document.getElementById('userSearchClear');
  if (searchInput) {
    searchInput.addEventListener('input', () => {
      clearTimeout(_searchTimer);
      _searchTimer = setTimeout(() => loadUsers(1, searchInput.value.trim()), 300);
    });
  }
  if (searchClear) {
    searchClear.addEventListener('click', () => {
      searchInput.value = '';
      loadUsers(1, '');
    });
  }

  // ── Load all dashboard data ────────────────
  await Promise.all([loadUtilization(), loadSkillDist(), loadUnutilized()]);

  // ── Utilization ────────────────────────────
  async function loadUtilization() {
    const res = await API.get('/admin/utilization');
    if (!res) return;
    const data = await res.json();

    let totalInterviewers = data.length;
    let totalInterviews   = 0;
    let totalConfirmed    = 0;
    let totalDeclined     = 0;

    const tbody = document.getElementById('utilizationTable');
    tbody.innerHTML = data.map(r => {
      totalInterviews += (r.confirmed + r.completed + r.declined);
      totalConfirmed  += r.confirmed;
      totalDeclined   += r.declined;

      const util = r.total_slots > 0
        ? Math.round((r.blocked_slots / r.total_slots) * 100) : 0;
      const barColor = util > 70 ? '#10b981' : util > 30 ? '#f59e0b' : '#ef4444';

      return `<tr>
        <td class="fw-semibold">${escHtml(r.interviewer_name)}</td>
        <td class="text-center">${r.total_slots}</td>
        <td class="text-center">${r.blocked_slots}
          <small class="text-muted">(${r.confirmed} conf, ${r.completed} done, ${r.declined} dec)</small>
        </td>
        <td class="text-center">${r.available_slots}</td>
        <td style="min-width:120px">
          <div class="d-flex align-items-center gap-2">
            <div class="util-bar flex-grow-1">
              <div class="util-bar-fill" style="width:${util}%;background:${barColor}"></div>
            </div>
            <small class="fw-semibold" style="color:${barColor};min-width:32px">${util}%</small>
          </div>
        </td>
      </tr>`;
    }).join('');

    document.getElementById('kpiInterviewers').textContent = totalInterviewers;
    document.getElementById('kpiTotal').textContent        = totalInterviews;
    document.getElementById('kpiConfirmed').textContent    = totalConfirmed;
    document.getElementById('kpiDeclined').textContent     = totalDeclined;
  }

  // ── Skill Distribution Chart ───────────────
  async function loadSkillDist() {
    const res = await API.get('/admin/skill-distribution');
    if (!res) return;
    const data = await res.json();
    if (data.length === 0) return;

    const COLORS = [
      '#3b82f6','#10b981','#f59e0b','#ef4444','#8b5cf6',
      '#06b6d4','#ec4899','#f97316','#84cc16','#6366f1'
    ];

    const ctx = document.getElementById('skillChart').getContext('2d');
    if (skillChart) skillChart.destroy();
    skillChart = new Chart(ctx, {
      type: 'doughnut',
      data: {
        labels: data.map(d => d.skill),
        datasets: [{
          data:  data.map(d => d.count),
          backgroundColor: COLORS.slice(0, data.length),
          borderWidth: 2, borderColor: '#fff'
        }]
      },
      options: {
        responsive: true, maintainAspectRatio: false,
        plugins: {
          legend: { position: 'right', labels: { boxWidth: 12, font: { size: 11 } } },
          tooltip: {
            callbacks: {
              label: ctx => ` ${ctx.label}: ${ctx.raw} interview${ctx.raw !== 1 ? 's' : ''}`
            }
          }
        }
      }
    });
  }

  // ── Unutilized Slots ───────────────────────
  async function loadUnutilized() {
    const res = await API.get('/admin/unutilized-slots');
    if (!res) return;
    const data = await res.json();
    const el   = document.getElementById('unutilizedContent');
    const total = data.reduce((acc, r) => acc + r.count, 0);
    document.getElementById('unutilizedTotal').textContent = `${total} slot${total !== 1 ? 's' : ''}`;

    if (data.length === 0) {
      el.innerHTML = '<div class="text-center text-muted py-3"><i class="bi bi-check-circle text-success fs-4 d-block mb-2"></i>No unutilized slots — great utilization!</div>';
      return;
    }
    el.innerHTML = `
      <div class="table-responsive">
        <table class="table table-hover align-middle">
          <thead class="table-light">
            <tr><th>Interviewer</th><th class="text-center">Unutilized Slots</th><th>Earliest</th><th>Latest</th></tr>
          </thead>
          <tbody>${data.map(r => `
            <tr>
              <td class="fw-semibold">${escHtml(r.interviewer_name)}</td>
              <td class="text-center"><span class="badge bg-warning text-dark">${r.count}</span></td>
              <td>${fmtDt(r.earliest)}</td>
              <td>${fmtDt(r.latest)}</td>
            </tr>`).join('')}
          </tbody>
        </table>
      </div>`;
  }

  // ── Users Table ────────────────────────────
  let _allUsers   = [];
  let _usersPage  = 1;
  let _searchTerm = '';
  let _searchTimer = null;

  async function loadUsers(page = 1, search = _searchTerm) {
    _usersPage  = page;
    _searchTerm = search;
    const params = new URLSearchParams({ page, page_size: 20 });
    if (search) params.set('search', search);

    const res = await API.get('/users?' + params.toString());
    if (!res) return;
    const data = await res.json();

    _allUsers = data.users || [];
    const total      = data.total      || 0;
    const totalPages = data.total_pages || 1;

    const tbody = document.getElementById('usersTable');
    tbody.innerHTML = _allUsers.map(u => `
      <tr>
        <td class="fw-semibold">${escHtml(u.full_name)}</td>
        <td><code>${escHtml(u.username)}</code></td>
        <td>${escHtml(u.email)}</td>
        <td>${roleBadge(u.role)}</td>
        <td>
          <button class="btn btn-sm btn-outline-primary py-0 px-2 edit-user-btn"
                  data-id="${u.id}" title="Edit user">
            <i class="bi bi-pencil"></i>
          </button>
        </td>
      </tr>`).join('');

    document.querySelectorAll('.edit-user-btn').forEach(btn => {
      btn.addEventListener('click', () => openEditModal(parseInt(btn.dataset.id)));
    });

    // Page info
    const from = total === 0 ? 0 : (_usersPage - 1) * 20 + 1;
    const to   = Math.min(_usersPage * 20, total);
    document.getElementById('usersPageInfo').textContent =
      total === 0 ? 'No users found' : `Showing ${from}–${to} of ${total} users`;

    // Pagination list
    const ul = document.getElementById('usersPaginationList');
    ul.innerHTML = '';
    if (totalPages <= 1) return;

    const mkItem = (label, p, disabled = false, active = false) => {
      const li = document.createElement('li');
      li.className = `page-item${disabled ? ' disabled' : ''}${active ? ' active' : ''}`;
      li.innerHTML = `<a class="page-link" href="#">${label}</a>`;
      if (!disabled && !active)
        li.querySelector('a').addEventListener('click', e => { e.preventDefault(); loadUsers(p); });
      ul.appendChild(li);
    };

    mkItem('&laquo;', _usersPage - 1, _usersPage === 1);
    const start = Math.max(1, _usersPage - 2);
    const end   = Math.min(totalPages, _usersPage + 2);
    for (let i = start; i <= end; i++) mkItem(i, i, false, i === _usersPage);
    mkItem('&raquo;', _usersPage + 1, _usersPage === totalPages);
  }

  function openEditModal(uid) {
    const u = _allUsers.find(x => x.id === uid);
    if (!u) return;
    document.getElementById('editUserId').value    = uid;
    document.getElementById('editFullName').value  = u.full_name;
    document.getElementById('editUsername').value  = u.username;
    document.getElementById('editEmail').value     = u.email;
    document.getElementById('editRole').value      = u.role;
    document.getElementById('editNewPassword').value = '';
    document.getElementById('editUserError').classList.add('d-none');
    document.getElementById('editUserSuccess').classList.add('d-none');
    const sess = JSON.parse(sessionStorage.getItem('session') || '{}');
    const delBtn = document.getElementById('deleteUserBtn');
    delBtn.disabled = (uid === sess.userId);
    delBtn.title    = (uid === sess.userId) ? 'Cannot delete your own account' : 'Delete this user';
    new bootstrap.Modal(document.getElementById('editUserModal')).show();
  }

  function roleBadge(role) {
    const map = { admin:'bg-dark', recruiter:'bg-primary', interviewer:'bg-success' };
    return `<span class="badge ${map[role]||'bg-secondary'} rounded-pill px-3">${role}</span>`;
  }

  // ── Create User ────────────────────────────
  document.getElementById('saveUserBtn').addEventListener('click', async () => {
    const fields = ['newFullName','newUsername','newEmail','newPassword','newRole'];
    const [fullName, username, email, password, role] =
      fields.map(id => document.getElementById(id).value.trim());

    const errDiv  = document.getElementById('createUserError');
    const spinner = document.getElementById('saveUserSpinner');
    const btn     = document.getElementById('saveUserBtn');
    errDiv.classList.add('d-none');

    if (!fullName || !username || !email || !password) {
      errDiv.textContent='All fields are required'; errDiv.classList.remove('d-none'); return;
    }

    btn.disabled=true; spinner.classList.remove('d-none');
    try {
      const res  = await API.post('/users', { full_name:fullName, username, email, password, role });
      const data = await res.json();
      if (!res.ok) { errDiv.textContent=data.error; errDiv.classList.remove('d-none'); return; }
      bootstrap.Modal.getInstance(document.getElementById('createUserModal')).hide();
      showToast('User created successfully!');
      fields.forEach(id => document.getElementById(id).value = '');
      loadUsers(1, _searchTerm);
    } finally {
      btn.disabled=false; spinner.classList.add('d-none');
    }
  });

  // ── Edit / Delete User ─────────────────────
  document.getElementById('updateUserBtn').addEventListener('click', async () => {
    const uid      = parseInt(document.getElementById('editUserId').value);
    const fullName = document.getElementById('editFullName').value.trim();
    const email    = document.getElementById('editEmail').value.trim();
    const role     = document.getElementById('editRole').value;
    const newPw    = document.getElementById('editNewPassword').value;
    const errDiv   = document.getElementById('editUserError');
    const sucDiv   = document.getElementById('editUserSuccess');
    const spinner  = document.getElementById('updateUserSpinner');
    const btn      = document.getElementById('updateUserBtn');
    errDiv.classList.add('d-none'); sucDiv.classList.add('d-none');

    if (!fullName || !email) {
      errDiv.textContent = 'Full name and email are required';
      errDiv.classList.remove('d-none'); return;
    }

    btn.disabled = true; spinner.classList.remove('d-none');
    try {
      const res  = await API.put(`/users/${uid}`, { full_name: fullName, email, role });
      const data = await res.json();
      if (!res.ok) { errDiv.textContent = data.error; errDiv.classList.remove('d-none'); return; }

      if (newPw) {
        if (newPw.length < 6) {
          errDiv.textContent = 'Password must be at least 6 characters';
          errDiv.classList.remove('d-none'); return;
        }
        const r2 = await API.post(`/users/${uid}/reset-password`, { new_password: newPw });
        const d2 = await r2.json();
        if (!r2.ok) { errDiv.textContent = d2.error; errDiv.classList.remove('d-none'); return; }
      }

      sucDiv.textContent = 'User updated successfully!';
      sucDiv.classList.remove('d-none');
      loadUsers(_usersPage, _searchTerm);
    } finally {
      btn.disabled = false; spinner.classList.add('d-none');
    }
  });

  document.getElementById('deleteUserBtn').addEventListener('click', async () => {
    const uid  = parseInt(document.getElementById('editUserId').value);
    const name = document.getElementById('editFullName').value.trim();
    if (!confirm(`Delete user "${name}"? This action cannot be undone.`)) return;
    const errDiv = document.getElementById('editUserError');
    errDiv.classList.add('d-none');
    const res  = await API.delete(`/users/${uid}`);
    const data = await res.json();
    if (!res.ok) { errDiv.textContent = data.error; errDiv.classList.remove('d-none'); return; }
    bootstrap.Modal.getInstance(document.getElementById('editUserModal')).hide();
    showToast('User deleted.');
    loadUsers(1, _searchTerm);
  });

  // ── SMTP Configuration ────────────────────
  async function loadSmtpConfig() {
    const res = await API.get('/admin/smtp-config');
    if (!res) return;
    const cfg = await res.json();
    document.getElementById('smtpEnabled').checked   = !!cfg.enabled;
    document.getElementById('smtpHost').value        = cfg.host       || '';
    document.getElementById('smtpPort').value        = cfg.port       || 25;
    document.getElementById('smtpUsername').value    = cfg.username   || '';
    document.getElementById('smtpPassword').value    = cfg.password   || '';
    document.getElementById('smtpFromEmail').value   = cfg.from_email || '';
    document.getElementById('smtpFromName').value    = cfg.from_name  || '';
  }

  function showSmtpAlert(msg, type = 'success') {
    const el = document.getElementById('smtpAlert');
    el.className = `alert alert-${type} mb-3`;
    el.textContent = msg;
    el.classList.remove('d-none');
    setTimeout(() => el.classList.add('d-none'), 6000);
  }

  document.getElementById('saveSmtpBtn').addEventListener('click', async () => {
    const btn     = document.getElementById('saveSmtpBtn');
    const spinner = document.getElementById('saveSmtpSpinner');
    btn.disabled = true; spinner.classList.remove('d-none');
    const body = {
      enabled:    document.getElementById('smtpEnabled').checked,
      host:       document.getElementById('smtpHost').value.trim(),
      port:       parseInt(document.getElementById('smtpPort').value) || 25,
      username:   document.getElementById('smtpUsername').value.trim(),
      password:   document.getElementById('smtpPassword').value,
      from_email: document.getElementById('smtpFromEmail').value.trim(),
      from_name:  document.getElementById('smtpFromName').value.trim() || 'Interview Scheduler',
    };
    try {
      const res  = await API.post('/admin/smtp-config', body);
      const data = await res.json();
      if (res.ok) showSmtpAlert('\u2705 ' + data.message, 'success');
      else        showSmtpAlert('\u274c ' + data.error,   'danger');
    } finally {
      btn.disabled = false; spinner.classList.add('d-none');
    }
  });

  document.getElementById('testSmtpBtn').addEventListener('click', async () => {
    const toEmail = prompt('Send test email to (enter your email address):');
    if (!toEmail) return;
    const btn     = document.getElementById('testSmtpBtn');
    const spinner = document.getElementById('testSmtpSpinner');
    btn.disabled = true; spinner.classList.remove('d-none');
    try {
      const res  = await API.post('/admin/smtp-test', { to_email: toEmail });
      const data = await res.json();
      if (res.ok) showSmtpAlert('\u2705 ' + data.message, 'success');
      else        showSmtpAlert('\u274c ' + (data.error || 'Failed'), 'danger');
    } finally {
      btn.disabled = false; spinner.classList.add('d-none');
    }
  });

  // ── Calendar ───────────────────────────────
  function initCalendar() {
    const el = document.getElementById('calendar');
    calendar = new FullCalendar.Calendar(el, {
      initialView: 'dayGridMonth',
      headerToolbar: { left:'prev,next today', center:'title', right:'dayGridMonth,timeGridWeek,listWeek' },
      height: 620,
      editable: false,
      validRange: {
        start: new Date(Date.now() - 30*24*60*60*1000).toISOString().split('T')[0],
        end:   new Date(Date.now() + 30*24*60*60*1000).toISOString().split('T')[0]
      },
      events: async (_info, success) => {
        const res = await API.get('/calendar');
        if (!res) return success([]);
        const evts = await res.json();
        success(evts.map(e => ({
          id: e.id, title: e.title,
          start: isoForFC(e.start), end: isoForFC(e.end),
          color: e.color,
          extendedProps: e.data || {}
        })));
      },
      eventClick: (info) => showEventDetail(info.event),
    });
    calendar.render();
  }

  function showEventDetail(event) {
    const d = event.extendedProps;
    document.getElementById('eventDetails').innerHTML = `
      <div class="detail-row"><span class="detail-label">Candidate</span><span>${escHtml(d.candidate_name||'')}</span></div>
      <div class="detail-row"><span class="detail-label">Email</span><span>${escHtml(d.candidate_email||'')}</span></div>
      <div class="detail-row"><span class="detail-label">Interviewer</span><span>${escHtml(d.interviewer_name||'')}</span></div>
      <div class="detail-row"><span class="detail-label">Recruiter</span><span>${escHtml(d.recruiter_name||'')}</span></div>
      <div class="detail-row"><span class="detail-label">Date</span><span>${fmtDt(d.start_time)}</span></div>
      <div class="detail-row"><span class="detail-label">Status</span><span>${statusBadge(d.status||'confirmed')}</span></div>
      ${d.decline_reason ? `<div class="detail-row"><span class="detail-label">Decline Reason</span><span class="text-danger">${escHtml(d.decline_reason)}</span></div>` : ''}
    `;
    const markBtn = document.getElementById('markCompleteBtn');
    currentInterviewId = d.id;
    markBtn.classList.toggle('d-none', d.status !== 'confirmed');
    new bootstrap.Modal(document.getElementById('eventModal')).show();
  }

  document.getElementById('markCompleteBtn').addEventListener('click', async () => {
    if (!currentInterviewId) return;
    const res = await API.put(`/interviews/${currentInterviewId}/complete`);
    if (!res || !res.ok) { const d=await res.json(); showToast(d.error,'danger'); return; }
    bootstrap.Modal.getInstance(document.getElementById('eventModal')).hide();
    showToast('Interview marked as completed!');
    if (calendar) calendar.refetchEvents();
    loadUtilization();
  });

  function escHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
  }

  // ── CSV Export (authenticated download) ───
  async function downloadCsv(endpoint, filename) {
    const token = localStorage.getItem('token');
    const res = await fetch('/api' + endpoint, {
      headers: { Authorization: `Bearer ${token}` }
    });
    if (!res.ok) { showToast('Export failed — ' + res.status, 'danger'); return; }
    const blob = await res.blob();
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href = url; a.download = filename;
    document.body.appendChild(a); a.click();
    setTimeout(() => { URL.revokeObjectURL(url); a.remove(); }, 1000);
  }

  document.getElementById('exportUtilBtn').addEventListener('click', () =>
    downloadCsv('/admin/export/utilization', 'utilization.csv'));

  document.getElementById('exportInterviewsBtn').addEventListener('click', () =>
    downloadCsv('/admin/export/interviews', 'interviews.csv'));

})();
