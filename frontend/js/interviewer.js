// ─────────────────────────────────────────────
//  interviewer.js
// ─────────────────────────────────────────────
(async () => {
  const user = requireAuth(['interviewer']);
  if (!user) return;
  initNavbar();

  let calendar;
  let mySlots   = [];
  let allSkills = [];
  let mySkills  = [];

  const declineModal  = new bootstrap.Modal(document.getElementById('declineModal'));
  const slotModal     = new bootstrap.Modal(document.getElementById('slotDetailModal'));
  const feedbackModal = new bootstrap.Modal(document.getElementById('feedbackModal'));

  await Promise.all([loadAllSkills(), loadMySkills(), loadMySlots()]);
  initCalendar();
  setupEvents();

  // ── Skills ─────────────────────────────────
  async function loadAllSkills() {
    const res = await API.get('/skills');
    if (!res) return;
    allSkills = await res.json();
  }

  // Render checkbox list inside the modal, excluding already-assigned skills
  function renderSkillCheckboxes(filterText = '') {
    const container = document.getElementById('skillCheckboxList');
    const emptyMsg  = document.getElementById('skillCheckboxEmpty');
    const myIds     = new Set(mySkills.map(s => s.id));

    const available = allSkills.filter(s => {
      if (myIds.has(s.id)) return false;
      if (filterText) return s.name.toLowerCase().includes(filterText.toLowerCase());
      return true;
    });

    if (available.length === 0) {
      container.innerHTML = `<div class="text-muted small text-center py-3">${
        filterText ? 'No skills match your filter' : 'All available skills are already assigned'
      }</div>`;
      updateSelectedCount();
      return;
    }

    container.innerHTML = available.map(s => `
      <div class="form-check py-1 skill-check-row">
        <input class="form-check-input skill-checkbox" type="checkbox"
               id="skill_cb_${s.id}" value="${s.id}" />
        <label class="form-check-label w-100" for="skill_cb_${s.id}" style="cursor:pointer">
          ${escHtml(s.name)}
        </label>
      </div>`).join('');

    container.querySelectorAll('.skill-checkbox').forEach(cb => {
      cb.addEventListener('change', updateSelectedCount);
    });
    updateSelectedCount();
  }

  function updateSelectedCount() {
    const checked = document.querySelectorAll('.skill-checkbox:checked').length;
    const el = document.getElementById('skillSelectedCount');
    if (el) el.textContent = checked === 0 ? '0 selected'
                           : `${checked} skill${checked > 1 ? 's' : ''} selected`;
  }

  async function loadMySkills() {
    const res = await API.get(`/interviewers/${user.id}/skills`);
    if (!res) return;
    mySkills = await res.json();
    renderSkills();
  }

  function renderSkills() {
    const el = document.getElementById('skillsList');
    if (mySkills.length === 0) {
      el.innerHTML = '<span class="text-muted small">No skills added yet</span>';
      return;
    }
    el.innerHTML = mySkills.map(s =>
      `<span class="badge bg-success-subtle text-success border border-success-subtle px-3 py-2 rounded-pill me-1 mb-1" style="font-size:.8rem">
         ${escHtml(s.name)}
         <button class="btn-close btn-close-sm ms-1" style="font-size:.55rem" data-skill-id="${s.id}" title="Remove"></button>
       </span>`
    ).join('');
    el.querySelectorAll('.btn-close').forEach(btn => {
      btn.addEventListener('click', () => removeSkill(parseInt(btn.dataset.skillId)));
    });
  }

  async function removeSkill(skillId) {
    const res = await API.delete(`/interviewers/${user.id}/skills/${skillId}`);
    if (!res || !res.ok) return showToast('Failed to remove skill', 'danger');
    showToast('Skill removed');
    await loadMySkills();
  }

  async function saveSkill() {
    const newName = document.getElementById('newSkillName').value.trim();
    const errD    = document.getElementById('skillModalError');
    const btn     = document.getElementById('saveSkillBtn');
    const spinner = document.getElementById('saveSkillSpinner');
    errD.classList.add('d-none');

    const checkedIds = [...document.querySelectorAll('.skill-checkbox:checked')]
                         .map(cb => parseInt(cb.value));

    if (checkedIds.length === 0 && !newName) {
      errD.textContent = 'Check at least one skill, or enter a new skill name.';
      errD.classList.remove('d-none');
      return;
    }

    btn.disabled = true;
    spinner.classList.remove('d-none');

    try {
      // 1. Create new skill if a name was typed
      if (newName) {
        const res = await API.post('/skills', { name: newName });
        const data = await res.json();
        if (!res.ok) {
          errD.textContent = data.error;
          errD.classList.remove('d-none');
          return;
        }
        checkedIds.push(data.id);
      }

      // 2. Add all selected skill IDs
      let added = 0, skipped = 0;
      for (const skillId of checkedIds) {
        const r = await API.post(`/interviewers/${user.id}/skills`, { skill_id: skillId });
        if (r && r.ok) added++;
        else skipped++;
      }

      bootstrap.Modal.getInstance(document.getElementById('skillModal')).hide();
      document.getElementById('newSkillName').value = '';
      const msg = added > 0
        ? `${added} skill${added > 1 ? 's' : ''} added!`
        : 'No new skills added.';
      showToast(msg, added > 0 ? 'success' : 'warning');
      await Promise.all([loadAllSkills(), loadMySkills()]);

    } finally {
      btn.disabled = false;
      spinner.classList.add('d-none');
    }
  }

  // ── Slots ──────────────────────────────────
  async function loadMySlots() {
    const res = await API.get(`/interviewers/${user.id}/slots`);
    if (!res) return;
    mySlots = await res.json();
    renderSlots();
    updateStats();
    if (calendar) calendar.refetchEvents();
  }

  function renderSlots() {
    const el = document.getElementById('mySlotsList');
    document.getElementById('slotsCount').textContent = mySlots.length;

    if (mySlots.length === 0) {
      el.innerHTML = '<div class="text-center text-muted py-4 small">No slots added yet</div>';
      return;
    }

    el.innerHTML = mySlots.map(s => `
      <div class="my-slot-item">
        <div>
          <div class="fw-semibold" style="font-size:.85rem">${fmtDt(s.start_time)}</div>
          ${s.interview_id ? `<div class="text-muted" style="font-size:.75rem"><i class="bi bi-person me-1"></i>${escHtml(s.candidate_name)}</div>` : ''}
        </div>
        <div class="d-flex align-items-center gap-2">
          <span class="badge rounded-pill slot-status ${s.status === 'blocked' ? 'status-blocked' : 'status-available'}">
            ${s.status === 'blocked' ? 'Booked' : 'Free'}
          </span>
          ${s.status === 'available'
            ? `<button class="btn btn-sm btn-outline-danger py-0 px-2" style="font-size:.75rem" data-delete-slot="${s.id}">
                 <i class="bi bi-trash"></i>
               </button>`
            : `<button class="btn btn-sm btn-outline-warning py-0 px-2" style="font-size:.75rem" data-view-slot="${s.id}">
                 View
               </button>`}
        </div>
      </div>`).join('');

    el.querySelectorAll('[data-delete-slot]').forEach(btn => {
      btn.addEventListener('click', () => deleteSlot(parseInt(btn.dataset.deleteSlot)));
    });
    el.querySelectorAll('[data-view-slot]').forEach(btn => {
      btn.addEventListener('click', () => viewSlot(parseInt(btn.dataset.viewSlot)));
    });
  }

  function updateStats() {
    document.getElementById('statAvailable').textContent = mySlots.filter(s => s.status === 'available').length;
    document.getElementById('statBooked').textContent    = mySlots.filter(s => s.status === 'blocked'  && s.interview_status === 'confirmed').length;
    document.getElementById('statCompleted').textContent = mySlots.filter(s => s.interview_status === 'completed').length;
  }

  async function addSlot() {
    const start   = document.getElementById('slotStart').value;
    const end     = document.getElementById('slotEnd').value;
    const errDiv  = document.getElementById('slotFormError');
    const spinner = document.getElementById('slotSpinner');
    const btn     = document.getElementById('addSlotBtn');
    errDiv.classList.add('d-none');

    if (!start || !end) { errDiv.textContent='Both start and end times are required'; errDiv.classList.remove('d-none'); return; }
    if (start >= end)   { errDiv.textContent='Start time must be before end time';     errDiv.classList.remove('d-none'); return; }

    // Enforce 30-day limit
    const maxDate = new Date(Date.now() + 30*24*60*60*1000);
    if (new Date(start) > maxDate) {
      errDiv.textContent = 'Slots cannot be created more than 30 days in advance';
      errDiv.classList.remove('d-none'); return;
    }

    btn.disabled = true; spinner.classList.remove('d-none');
    try {
      const res  = await API.post(`/interviewers/${user.id}/slots`, { start_time: start, end_time: end });
      const data = await res.json();
      if (!res.ok) { errDiv.textContent=data.error; errDiv.classList.remove('d-none'); return; }
      document.getElementById('slotStart').value = '';
      document.getElementById('slotEnd').value   = '';
      showToast('Availability slot added!');
      await loadMySlots();
    } finally {
      btn.disabled = false; spinner.classList.add('d-none');
    }
  }

  async function deleteSlot(slotId) {
    if (!confirm('Delete this availability slot?')) return;
    const res = await API.delete(`/slots/${slotId}`);
    if (!res.ok) { const d=await res.json(); showToast(d.error,'danger'); return; }
    showToast('Slot deleted'); await loadMySlots();
  }

  function viewSlot(slotId) {
    const slot = mySlots.find(s => s.id === slotId);
    if (!slot) return;
    document.getElementById('slotDetailContent').innerHTML = `
      <div class="detail-row"><span class="detail-label">Date & Time</span><span>${fmtDt(slot.start_time)}</span></div>
      <div class="detail-row"><span class="detail-label">End Time</span><span>${fmtDt(slot.end_time)}</span></div>
      <div class="detail-row"><span class="detail-label">Status</span><span>${statusBadge(slot.status)}</span></div>
      ${slot.interview_id
        ? `<div class="detail-row"><span class="detail-label">Candidate</span><span>${escHtml(slot.candidate_name)}</span></div>
           <div class="detail-row"><span class="detail-label">Interview</span><span>${statusBadge(slot.interview_status)}</span></div>`
        : ''}
    `;
    const actions = document.getElementById('slotDetailActions');
    actions.innerHTML = '';
    if (slot.interview_id && slot.interview_status === 'confirmed') {
      const decBtn = document.createElement('button');
      decBtn.className = 'btn btn-danger btn-sm';
      decBtn.innerHTML = '<i class="bi bi-x-circle me-1"></i>Decline Interview';
      decBtn.onclick = () => { slotModal.hide(); openDeclineModal(slot.interview_id); };
      actions.appendChild(decBtn);

      const cmpBtn = document.createElement('button');
      cmpBtn.className = 'btn btn-success btn-sm';
      cmpBtn.innerHTML = '<i class="bi bi-check-circle me-1"></i>Complete & Feedback';
      cmpBtn.onclick = () => { slotModal.hide(); openFeedbackModal(slot.interview_id); };
      actions.appendChild(cmpBtn);
    }
    // Show existing feedback if completed
    if (slot.interview_id && slot.interview_status === 'completed') {
      const fbBtn = document.createElement('button');
      fbBtn.className = 'btn btn-warning btn-sm';
      fbBtn.innerHTML = '<i class="bi bi-star me-1"></i>View/Edit Feedback';
      fbBtn.onclick = () => { slotModal.hide(); openFeedbackModal(slot.interview_id, true); };
      actions.appendChild(fbBtn);
    }
    slotModal.show();
  }

  function openDeclineModal(interviewId) {
    document.getElementById('declineInterviewId').value = interviewId;
    document.getElementById('declineReason').value = '';
    document.getElementById('declineError').classList.add('d-none');
    declineModal.show();
  }

  async function openFeedbackModal(interviewId, viewOnly = false) {
    document.getElementById('feedbackInterviewId').value = interviewId;
    document.getElementById('feedbackRating').value = '0';
    document.getElementById('feedbackNotes').value  = '';
    document.getElementById('feedbackError').classList.add('d-none');
    // Reset star buttons
    document.querySelectorAll('.star-btn').forEach(b => {
      b.className = 'btn btn-outline-warning star-btn';
      b.innerHTML = `<i class="bi bi-star"></i> ${b.dataset.val}`;
    });
    // Pre-load existing feedback if any
    try {
      const res = await API.get(`/interviews/${interviewId}/feedback`);
      if (res && res.ok) {
        const fb = await res.json();
        if (fb.rating) {
          document.getElementById('feedbackRating').value = fb.rating;
          highlightStars(fb.rating);
        }
        if (fb.notes) document.getElementById('feedbackNotes').value = fb.notes;
      }
    } catch(_) {}
    feedbackModal.show();
  }

  function highlightStars(val) {
    document.querySelectorAll('.star-btn').forEach(b => {
      const v = parseInt(b.dataset.val);
      if (v <= val) {
        b.className = 'btn btn-warning star-btn';
        b.innerHTML = `<i class="bi bi-star-fill"></i> ${v}`;
      } else {
        b.className = 'btn btn-outline-warning star-btn';
        b.innerHTML = `<i class="bi bi-star"></i> ${v}`;
      }
    });
  }

  async function submitDecline() {
    const ivId   = document.getElementById('declineInterviewId').value;
    const reason = document.getElementById('declineReason').value.trim();
    const errDiv = document.getElementById('declineError');
    const spinner= document.getElementById('declineSpinner');
    const btn    = document.getElementById('submitDeclineBtn');
    errDiv.classList.add('d-none');

    if (!reason) { errDiv.textContent='Please provide a reason'; errDiv.classList.remove('d-none'); return; }

    btn.disabled=true; spinner.classList.remove('d-none');
    try {
      const res = await API.put(`/interviews/${ivId}/decline`, { reason });
      const data = await res.json();
      if (!res.ok) { errDiv.textContent=data.error; errDiv.classList.remove('d-none'); return; }
      declineModal.hide();
      showToast('Interview declined. Recruiter has been notified.');
      await loadMySlots();
    } finally {
      btn.disabled=false; spinner.classList.add('d-none');
    }
  }

  // ── Calendar ───────────────────────────────
  function initCalendar() {
    const el = document.getElementById('calendar');
    calendar = new FullCalendar.Calendar(el, {
      initialView: 'timeGridWeek',
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
          id: e.id, title: e.title,
          start: isoForFC(e.start), end: isoForFC(e.end),
          color: e.color,
          extendedProps: { ...e.slotData, type: e.type }
        })));
      },
      eventClick: (info) => {
        const p = info.event.extendedProps;
        if (p.type === 'slot' && p.status === 'blocked' && p.interview_id) {
          viewSlot(p.id);
        }
      }
    });
    calendar.render();
  }

  // ── Setup ──────────────────────────────────
  function setupEvents() {
    // Star rating buttons
    document.querySelectorAll('.star-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const val = parseInt(btn.dataset.val);
        document.getElementById('feedbackRating').value = val;
        highlightStars(val);
      });
    });

    // Submit feedback + complete
    document.getElementById('submitFeedbackBtn').addEventListener('click', async () => {
      const ivId   = document.getElementById('feedbackInterviewId').value;
      const rating = parseInt(document.getElementById('feedbackRating').value);
      const notes  = document.getElementById('feedbackNotes').value.trim();
      const errDiv = document.getElementById('feedbackError');
      const spinner= document.getElementById('feedbackSpinner');
      const btn    = document.getElementById('submitFeedbackBtn');
      errDiv.classList.add('d-none');

      if (!rating || rating < 1) {
        errDiv.textContent = 'Please select a rating (1-5)';
        errDiv.classList.remove('d-none'); return;
      }

      btn.disabled = true; spinner.classList.remove('d-none');
      try {
        // First: mark complete (may already be done)
        const slot = mySlots.find(s => s.interview_id == ivId);
        if (slot && slot.interview_status === 'confirmed') {
          const r1 = await API.put(`/interviews/${ivId}/complete`, {});
          if (!r1.ok) { const d=await r1.json(); errDiv.textContent=d.error; errDiv.classList.remove('d-none'); return; }
        }
        // Then: submit feedback
        const r2 = await API.post(`/interviews/${ivId}/feedback`, { rating, notes });
        const d2 = await r2.json();
        if (!r2.ok) { errDiv.textContent=d2.error; errDiv.classList.remove('d-none'); return; }
        feedbackModal.hide();
        showToast('Interview completed and feedback submitted!');
        await loadMySlots();
      } finally {
        btn.disabled = false; spinner.classList.add('d-none');
      }
    });
    document.getElementById('addSlotBtn').addEventListener('click', addSlot);
    document.getElementById('saveSkillBtn').addEventListener('click', saveSkill);
    document.getElementById('submitDeclineBtn').addEventListener('click', submitDecline);

    // Skill modal wiring
    document.getElementById('skillModal').addEventListener('show.bs.modal', () => {
      document.getElementById('skillSearch').value = '';
      document.getElementById('newSkillName').value = '';
      document.getElementById('skillModalError').classList.add('d-none');
      renderSkillCheckboxes('');
    });
    document.getElementById('skillSearch').addEventListener('input', e => {
      renderSkillCheckboxes(e.target.value.trim());
    });
    document.getElementById('selectAllSkills').addEventListener('click', () => {
      document.querySelectorAll('.skill-checkbox').forEach(cb => { cb.checked = true; });
      updateSelectedCount();
    });
    document.getElementById('deselectAllSkills').addEventListener('click', () => {
      document.querySelectorAll('.skill-checkbox').forEach(cb => { cb.checked = false; });
      updateSelectedCount();
    });
  }

  function escHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
  }
})();
