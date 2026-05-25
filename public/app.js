const state = {
  resources: [],
  internships: [],
  users: [],
  semester: null,
  search: "",
  type: "All"
};

const dom = {
  resourceList: document.querySelector("#resource-list"),
  internshipList: document.querySelector("#internship-list"),
  resourceSelect: document.querySelector("#resource-id"),
  offerForm: document.querySelector("#offer-form"),
  listingForm: document.querySelector("#listing-form"),
  offerStatus: document.querySelector("#offer-status"),
  walletStatus: document.querySelector("#wallet-status"),
  listingStatus: document.querySelector("#listing-status"),
  refresh: document.querySelector("#refresh-button"),
  activeCount: document.querySelector("#active-count"),
  offerCount: document.querySelector("#offer-count"),
  listedCount: document.querySelector("#listed-count"),
  template: document.querySelector("#resource-template"),
  search: document.querySelector("#search"),
  typeFilter: document.querySelector("#type-filter")
};

function isKnownView(viewId) {
  return Boolean(document.getElementById(viewId));
}

function viewFromLocation() {
  const viewId = window.location.hash.replace("#", "");
  return isKnownView(viewId) ? viewId : "dashboard";
}

function viewUrl(viewId) {
  const url = new URL(window.location.href);
  url.hash = viewId === "dashboard" ? "" : viewId;
  return url;
}

function openView(viewId, options = {}) {
  if (!isKnownView(viewId)) return;

  const settings = {
    updateHistory: true,
    scroll: true,
    ...options
  };

  document.querySelectorAll("[data-view]").forEach((view) => {
    view.classList.toggle("active", view.id === viewId);
  });

  document.querySelectorAll("[data-view-target]").forEach((control) => {
    control.classList.toggle("active", control.dataset.viewTarget === viewId);
  });

  if (settings.updateHistory && viewId !== viewFromLocation()) {
    history.pushState({ viewId }, "", viewUrl(viewId));
  }

  document.title = viewId === "dashboard"
    ? "Smart Resource Exchange"
    : `${document.querySelector(`#${viewId} h1`)?.textContent || "Feature"} | Smart Resource Exchange`;

  if (settings.scroll) {
    window.scrollTo({ top: 0, behavior: "smooth" });
  }
}

function showStatus(element, message, isError = false) {
  element.textContent = message;
  element.classList.toggle("error", isError);
  element.classList.remove("hidden");
}

function formatDeadline(value) {
  const date = new Date(value * 1000);
  return new Intl.DateTimeFormat(undefined, {
    month: "short",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit"
  }).format(date);
}

function isOpen(resource) {
  return resource.allocatedTo === "";
}

function filteredResources() {
  const query = state.search.toLowerCase();
  return state.resources.filter((resource) => {
    const matchesType = state.type === "All" || resource.type === state.type;
    const haystack = `${resource.title} ${resource.owner} ${resource.description}`.toLowerCase();
    return matchesType && haystack.includes(query);
  });
}

function selectResource(resourceId) {
  dom.resourceSelect.value = String(resourceId);
  syncOfferMode();
  openView("request");
}

function selectedResource() {
  const resourceId = Number(dom.resourceSelect.value);
  return state.resources.find((resource) => resource.id === resourceId);
}

function selectedUser() {
  const userName = document.querySelector("#student-name").value;
  return state.users.find((user) => user.name === userName);
}

function chooseEligibleUser(resource) {
  const userSelect = document.querySelector("#student-name");
  const currentUser = selectedUser();
  if (currentUser && currentUser.id !== resource.ownerUserId) {
    return currentUser;
  }

  const nextUser = state.users.find((user) => user.id !== resource.ownerUserId);
  if (nextUser) {
    userSelect.value = nextUser.name;
  }
  return nextUser || currentUser;
}

function syncOfferMode() {
  const resource = selectedResource();
  const user = resource ? chooseEligibleUser(resource) : selectedUser();
  if (!resource || !user) return;

  const modeInput = document.querySelector("#mode");
  const bidInput = document.querySelector("#bid-value");
  const creditsInput = document.querySelector("#credits");
  const walletScore = Math.min(user.availableCredits, 50);

  modeInput.value = resource.mode;
  modeInput.disabled = true;
  creditsInput.value = String(walletScore);
  bidInput.disabled = resource.mode === "Exchange";
  bidInput.required = resource.mode === "Bidding";
  bidInput.max = String(user.availableCredits);
  if (resource.mode === "Exchange") {
    bidInput.value = "0";
  }

  const semesterName = state.semester?.name || "active semester";
  showStatus(
    dom.walletStatus,
    `${user.name}: ${user.availableCredits} available credits (${user.lockedCredits} locked) for ${semesterName}.`
  );
}

function renderResources() {
  dom.resourceList.innerHTML = "";
  dom.resourceSelect.innerHTML = "";

  let active = 0;
  let offerTotal = 0;

  state.resources.forEach((resource) => {
    if (isOpen(resource)) {
      active += 1;
      const option = document.createElement("option");
      option.value = resource.id;
      option.textContent = resource.title;
      dom.resourceSelect.append(option);
    }
    offerTotal += resource.offerCount;
  });

  filteredResources().forEach((resource) => {
    const closed = !isOpen(resource);
    const fragment = dom.template.content.cloneNode(true);
    fragment.querySelector(".resource-type").textContent = `${resource.type} listed by ${resource.owner}`;
    fragment.querySelector("h3").textContent = resource.title;
    fragment.querySelector(".description").textContent = resource.description;
    fragment.querySelector(".mode").textContent = resource.mode;
    fragment.querySelector(".deadline").textContent = formatDeadline(resource.deadline);
    fragment.querySelector(".offers").textContent = String(resource.offerCount);
    fragment.querySelector(".winner").textContent = closed ? resource.allocatedTo : "Pending";

    const badge = fragment.querySelector(".badge");
    badge.textContent = closed ? `Allocated: ${resource.bestScore}` : resource.urgency;
    badge.classList.toggle("closed", closed);

    const button = fragment.querySelector(".request-button");
    button.disabled = closed;
    button.textContent = closed ? "Closed" : "Request this";
    button.addEventListener("click", () => selectResource(resource.id));

    dom.resourceList.append(fragment);
  });

  if (!dom.resourceList.children.length) {
    const empty = document.createElement("p");
    empty.className = "empty-state";
    empty.textContent = "No matching resources found.";
    dom.resourceList.append(empty);
  }

  if (!dom.resourceSelect.children.length) {
    const option = document.createElement("option");
    option.textContent = "No open listings";
    option.value = "";
    dom.resourceSelect.append(option);
  }

  syncOfferMode();

  dom.activeCount.textContent = String(active);
  dom.offerCount.textContent = String(offerTotal);
  dom.listedCount.textContent = String(state.resources.length);
}

function renderInternships() {
  dom.internshipList.innerHTML = "";
  state.internships.forEach((item) => {
    const article = document.createElement("article");
    article.innerHTML = `
      <h3>${item.company}</h3>
      <p>${item.title}</p>
      <span>${item.deadline}</span>
    `;
    dom.internshipList.append(article);
  });
}

function renderUsers() {
  const userSelect = document.querySelector("#student-name");
  const previousValue = userSelect.value;
  userSelect.innerHTML = "";

  state.users.forEach((user) => {
    const option = document.createElement("option");
    option.value = user.name;
    option.textContent = `${user.name} (${user.availableCredits} credits available)`;
    userSelect.append(option);
  });

  if (state.users.some((user) => user.name === previousValue)) {
    userSelect.value = previousValue;
  }

  syncOfferMode();
}

async function refreshData() {
  const response = await fetch("/api/resources");
  const payload = await response.json();
  state.resources = payload.resources || [];
  state.internships = payload.internships || [];
  state.users = payload.users || [];
  state.semester = payload.semester || null;
  renderResources();
  renderUsers();
  renderInternships();
}

dom.offerForm.addEventListener("submit", async (event) => {
  event.preventDefault();

  const payload = {
    resourceId: Number(dom.resourceSelect.value),
    studentName: document.querySelector("#student-name").value.trim(),
    urgency: document.querySelector("#urgency").value,
    mode: document.querySelector("#mode").value,
    bidValue: Number(document.querySelector("#bid-value").value)
  };

  const response = await fetch("/api/offers", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
  const data = await response.json();

  if (!response.ok) {
    showStatus(dom.offerStatus, data.error || "Offer could not be submitted.", true);
    return;
  }

  showStatus(dom.offerStatus, `Offer accepted. Priority score: ${data.score}. Available credits: ${data.availableCredits}`);
  await refreshData();
});

dom.listingForm.addEventListener("submit", async (event) => {
  event.preventDefault();

  const payload = {
    title: document.querySelector("#listing-title").value.trim(),
    owner: document.querySelector("#listing-owner").value.trim(),
    type: document.querySelector("#listing-type").value,
    mode: document.querySelector("#listing-mode").value,
    urgency: document.querySelector("#listing-urgency").value,
    durationMinutes: Number(document.querySelector("#listing-duration").value),
    description: document.querySelector("#listing-description").value.trim()
  };

  const response = await fetch("/api/resources", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
  const data = await response.json();

  if (!response.ok) {
    showStatus(dom.listingStatus, data.error || "Resource could not be listed.", true);
    return;
  }

  dom.listingForm.reset();
  showStatus(dom.listingStatus, `Resource published. Listing ID: ${data.id}`);
  await refreshData();
});

dom.refresh.addEventListener("click", async () => {
  await refreshData();
  showStatus(dom.offerStatus, "Listings refreshed.");
});

dom.search.addEventListener("input", () => {
  state.search = dom.search.value;
  renderResources();
});

dom.typeFilter.addEventListener("change", () => {
  state.type = dom.typeFilter.value;
  renderResources();
});

dom.resourceSelect.addEventListener("change", syncOfferMode);
document.querySelector("#student-name").addEventListener("change", syncOfferMode);

document.querySelectorAll("[data-view-target]").forEach((control) => {
  control.addEventListener("click", () => openView(control.dataset.viewTarget));
});

history.replaceState({ viewId: viewFromLocation() }, "", viewUrl(viewFromLocation()));
openView(viewFromLocation(), { updateHistory: false, scroll: false });

window.addEventListener("popstate", (event) => {
  openView(event.state?.viewId || viewFromLocation(), {
    updateHistory: false,
    scroll: false
  });
});

window.addEventListener("hashchange", () => {
  openView(viewFromLocation(), {
    updateHistory: false,
    scroll: false
  });
});

refreshData().catch(() => {
  showStatus(dom.offerStatus, "Could not connect to the C++ backend.", true);
});
