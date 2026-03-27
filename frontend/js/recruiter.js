// ─────────────────────────────────────────────
//  recruiter.js
// ─────────────────────────────────────────────
(async () => {
  const user = requireAuth(['recruiter']);
  if (!user) return;
  initNavbar();

  let calendar;
  let currentSlots = [];
  let allSkills    = [];

  // ── Init ───────────────────────────────────
  await Promise.all([loadSkills(), loadStats(), loadFeedback()]);
  initCalendar();
  setupEventListeners();

  // ── Skills for filter dropdown ─────────────
  async function loadSkills() {
    const res = await API.get('/skills');
    if (!res) return;
    allSkills = await res.json();
    const sel = document.getElementById('filterSkill');
    allSkills.forEach(s => {
      const opt = document.createElement('option');
      opt.value = s.name; opt.textContent = s.name;
      sel.appendChild(opt);
    });
  }

  // ── Stats / KPI ────────────────────────────
  async function loadStats() {
    const res = await API.get('/interviews');
    if (!res) return;
    const interviews = await res.json();
    document.getElementById('statTotal').textContent     = interviews.length;
    document.getElementById('statConfirmed').textContent = interviews.filter(i => i.status === 'confirmed').length;
    document.getElementById('statDeclined').textContent  = interviews.filter(i => i.status === 'declined').length;
    if (calendar) calendar.refetchEvents();
  }

  // ── Search Available Slots ─────────────────
  async function searchSlots() {
    const skill = document.getElementById('filterSkill').value;
    const date  = document.getElementById('filterDate').value;
    const btn   = document.getElementById('searchBtn');
    btn.disabled = true; btn.innerHTML = '<span class="spinner-border spinner-border-sm"></span> Searching…';

    try {
      let url = '/available-slots';
      const params = [];
      if (skill) params.push('skill=' + encodeURIComponent(skill));
      if (date)  params.push('date='  + encodeURIComponent(date));
      if (params.length) url += '?' + params.join('&');

      const res = await API.get(url);
      if (!res) return;
      currentSlots = await res.json();

      const list = document.getElementById('slotsList');
      document.getElementById('slotCount').textContent = currentSlots.length;

      if (currentSlots.length === 0) {
        list.innerHTML =
          '<div class="text-center text-muted py-4"><i class="bi bi-calendar-x fs-3 d-block mb-2 opacity-50"></i>No available slots found</div>';
        return;
      }

      list.innerHTML = currentSlots.map(s => `
        <div class="slot-card" data-id="${s.id}">
          <div class="d-flex justify-content-between align-items-start mb-1">
            <div class="slot-interviewer">${escHtml(s.interviewer_name)}</div>
            <button class="btn btn-primary btn-sm btn-book" data-id="${s.id}">Book</button>
          </div>
          <div class="slot-time"><i class="bi bi-clock me-1"></i>${fmtDt(s.start_time)} → ${fmtDt(s.end_time)}</div>
          ${s.skills ? `<div class="slot-skills mt-1"><i class="bi bi-tags me-1"></i>${escHtml(s.skills)}</div>` : ''}
        </div>`).join('');

      list.querySelectorAll('.btn-book').forEach(btn => {
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          openBookModal(parseInt(btn.dataset.id));
        });
      });

    } finally {
      btn.disabled = false; btn.innerHTML = '<i class="bi bi-search me-1"></i>Search Slots';
    }
  }

  // ── Book Modal ─────────────────────────────
  function openBookModal(slotId) {
    const slot = currentSlots.find(s => s.id === slotId);
    if (!slot) return;
    document.getElementById('bookSlotId').value = slotId;
    document.getElementById('bookSlotInfo').innerHTML =
      `<strong>${escHtml(slot.interviewer_name)}</strong><br>
       <span class="text-muted small">${fmtDt(slot.start_time)} → ${fmtDt(slot.end_time)}</span>`;
    document.getElementById('candidateName').value  = '';
    document.getElementById('candidateEmail').value = '';
    document.getElementById('bookError').classList.add('d-none');
    new bootstrap.Modal(document.getElementById('bookModal')).show();
  }

  async function confirmBook() {
    const slotId   = parseInt(document.getElementById('bookSlotId').value);
    const name     = document.getElementById('candidateName').value.trim();
    const email    = document.getElementById('candidateEmail').value.trim();
    const errDiv   = document.getElementById('bookError');
    const spinner  = document.getElementById('bookSpinner');
    const btn      = document.getElementById('confirmBookBtn');

    errDiv.classList.add('d-none');
    if (!name)  { errDiv.textContent='Candidate name is required'; errDiv.classList.remove('d-none'); return; }
    if (!email) { errDiv.textContent='Candidate email is required'; errDiv.classList.remove('d-none'); return; }

    btn.disabled = true; spinner.classList.remove('d-none');
    try {
      const res = await API.post('/interviews', {
        slot_id: slotId, candidate_name: name, candidate_email: email
      });
      const data = await res.json();
      if (!res.ok) { errDiv.textContent = data.error; errDiv.classList.remove('d-none'); return; }

      bootstrap.Modal.getInstance(document.getElementById('bookModal')).hide();
      showToast('Interview booked successfully!');
      await Promise.all([searchSlots(), loadStats()]);
    } finally {
      btn.disabled = false; spinner.classList.add('d-none');
    }
  }

  // ── Calendar ───────────────────────────────
  function initCalendar() {
    const el = document.getElementById('calendar');
    calendar = new FullCalendar.Calendar(el, {
      initialView: 'dayGridMonth',
      headerToolbar: { left:'prev,next today', center:'title', right:'dayGridMonth,timeGridWeek,listWeek' },
      height: 560,
      validRange: {
        start: new Date(Date.now() - 30*24*60*60*1000).toISOString().split('T')[0],
        end:   new Date(Date.now() + 30*24*60*60*1000).toISOString().split('T')[0]
      },
      events: async (_info, success) => {
        const res = await API.get('/calendar');
        if (!res) return success([]);
        const evts = await res.json();
        success(evts.map(e => ({
          id:    e.id,
          title: e.title,
          start: isoForFC(e.start),
          end:   isoForFC(e.end),
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
    const status = d.status || 'confirmed';
    const detailEl = document.getElementById('eventDetails');
    detailEl.innerHTML = `
      <div class="detail-row"><span class="detail-label">Candidate</span><span>${escHtml(d.candidate_name||'')}</span></div>
      <div class="detail-row"><span class="detail-label">Email</span><span>${escHtml(d.candidate_email||'')}</span></div>
      <div class="detail-row"><span class="detail-label">Interviewer</span><span>${escHtml(d.interviewer_name||'')}</span></div>
      <div class="detail-row"><span class="detail-label">Date</span><span>${fmtDt(d.start_time)}</span></div>
      <div class="detail-row"><span class="detail-label">Status</span><span>${statusBadge(status)}</span></div>
      ${d.decline_reason ? `<div class="detail-row"><span class="detail-label">Decline Reason</span><span class="text-danger">${escHtml(d.decline_reason)}</span></div>` : ''}
    `;
    if (status === 'completed' && d.id) {
      API.get(`/interviews/${d.id}/feedback`).then(res => {
        if (!res || !res.ok) return;
        res.json().then(fb => {
          const stars = '★'.repeat(fb.rating) + '☆'.repeat(5 - fb.rating);
          detailEl.insertAdjacentHTML('beforeend',
            `<hr class="my-2">
             <div class="detail-row"><span class="detail-label">Interviewer Rating</span><span class="text-warning fw-bold">${stars} (${fb.rating}/5)</span></div>
             ${fb.notes ? `<div class="detail-row"><span class="detail-label">Feedback Notes</span><span class="text-muted">${escHtml(fb.notes)}</span></div>` : ''}`);
        });
      });
    }
    new bootstrap.Modal(document.getElementById('eventModal')).show();
  }

  // ── Feedback ────────────────────────────────
  async function loadFeedback() {
    const res = await API.get('/feedback');
    const el = document.getElementById('feedbackList');
    if (!el) return;
    if (!res || !res.ok) {
      el.innerHTML = '<div class="text-muted text-center py-3 small">Could not load feedback.</div>';
      return;
    }
    const list = await res.json();
    if (!list.length) {
      el.innerHTML = '<div class="text-muted text-center py-3 small">No feedback received yet.</div>';
      return;
    }
    el.innerHTML = list.map(f => {
      const stars = '\u2605'.repeat(f.rating) + '\u2606'.repeat(5 - f.rating);
      return `<div class="border rounded-3 p-3 mb-2">
        <div class="d-flex justify-content-between align-items-start">
          <strong>${escHtml(f.candidate_name)}</strong>
          <span class="text-warning fs-5">${stars}</span>
        </div>
        <div class="text-muted small">${escHtml(f.interviewer_name)} &middot; ${(f.start_time||'').substring(0,10)}</div>
        ${f.notes ? `<p class="mb-0 mt-1 small">${escHtml(f.notes)}</p>` : ''}
      </div>`;
    }).join('');
  }

  // ── Event listeners ────────────────────────
  function setupEventListeners() {
    document.getElementById('searchBtn').addEventListener('click', searchSlots);
    document.getElementById('filterDate').addEventListener('keydown', e => { if (e.key==='Enter') searchSlots(); });
    document.getElementById('confirmBookBtn').addEventListener('click', confirmBook);
  }

  function escHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
  }
})();
