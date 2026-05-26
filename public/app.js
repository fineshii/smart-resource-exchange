const state = {
  resources: [],
  internships: [],
  users: [],
  semester: null,
  currentUser: JSON.parse(localStorage.getItem("smartExchangeUser") || "null"),
  pendingView: null,
  offerContextKey: null,
  search: "",
  type: "All"
};

const publicViews = new Set(["auth"]);

const dom = {
  resourceList: document.querySelector("#resource-list"),
  internshipList: document.querySelector("#internship-list"),
  resourceSelect: document.querySelector("#resource-id"),
  offerForm: document.querySelector("#offer-form"),
  listingForm: document.querySelector("#listing-form"),
  registerForm: document.querySelector("#register-form"),
  loginForm: document.querySelector("#login-form"),
  logoutButton: document.querySelector("#logout-button"),
  offerStatus: document.querySelector("#offer-status"),
  walletStatus: document.querySelector("#wallet-status"),
  listingStatus: document.querySelector("#listing-status"),
  registerStatus: document.querySelector("#register-status"),
  loginStatus: document.querySelector("#login-status"),
  accountTitle: document.querySelector("#account-title"),
  accountSummary: document.querySelector("#account-summary"),
  offerAccount: document.querySelector("#offer-account"),
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

function rawViewFromLocation() {
  const viewId = window.location.hash.replace("#", "");
  return isKnownView(viewId) ? viewId : "dashboard";
}

function isProtectedView(viewId) {
  return !publicViews.has(viewId);
}

function routeFor(viewId) {
  const requestedView = isKnownView(viewId) ? viewId : "dashboard";
  if (!state.currentUser && isProtectedView(requestedView)) {
    state.pendingView = requestedView;
    return "auth";
  }
  return requestedView;
}

function viewFromLocation() {
  return routeFor(rawViewFromLocation());
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
    replaceHistory: false,
    ...options
  };
  const targetView = routeFor(viewId);

  if (targetView === "auth" && viewId !== "auth" && !state.currentUser) {
    showStatus(dom.loginStatus, "Log in to continue.", true);
  }

  document.querySelectorAll("[data-view]").forEach((view) => {
    view.classList.toggle("active", view.id === targetView);
  });

  document.querySelectorAll("[data-view-target]").forEach((control) => {
    control.classList.toggle("active", control.dataset.viewTarget === targetView);
  });

  if (settings.updateHistory && targetView !== rawViewFromLocation()) {
    const nextState = { viewId: targetView };
    if (settings.replaceHistory) {
      history.replaceState(nextState, "", viewUrl(targetView));
    } else {
      history.pushState(nextState, "", viewUrl(targetView));
    }
  }

  document.title = targetView === "dashboard"
    ? "Smart Resource Exchange"
    : `${document.querySelector(`#${targetView} h1`)?.textContent || "Feature"} | Smart Resource Exchange`;

  if (settings.scroll) {
    window.scrollTo({ top: 0, behavior: "smooth" });
  }
}

function showStatus(element, message, isError = false) {
  element.textContent = message;
  element.classList.toggle("error", isError);
  element.classList.remove("hidden");
}

function clearStatus(element) {
  element.textContent = "";
  element.classList.remove("error");
  element.classList.add("hidden");
}

function clearRequestResult() {
  clearStatus(dom.offerStatus);
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
  return resource.allocatedTo === "" && Date.now() < resource.deadline * 1000;
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
  clearRequestResult();
  syncOfferMode();
  openView("request");
}

function selectedResource() {
  const resourceId = Number(dom.resourceSelect.value);
  return state.resources.find((resource) => resource.id === resourceId);
}

function selectedUser() {
  if (!state.currentUser) return null;
  return state.users.find((user) => user.id === state.currentUser.id) || state.currentUser;
}

function syncUrgencyLimit(user) {
  const urgencyInput = document.querySelector("#urgency");
  const highOption = urgencyInput.querySelector("option[value='High']");
  const remaining = user?.highUrgencyRemaining ?? 0;
  highOption.disabled = !user || remaining <= 0;
  highOption.textContent = user
    ? `High (${remaining} left)`
    : "High";
  if (highOption.disabled && urgencyInput.value === "High") {
    urgencyInput.value = "Medium";
  }
}

function syncOfferMode() {
  const contextKey = `${state.currentUser?.id || "guest"}:${dom.resourceSelect.value || "none"}`;
  if (state.offerContextKey !== contextKey) {
    clearRequestResult();
    state.offerContextKey = contextKey;
  }

  const resource = selectedResource();
  const user = selectedUser();
  const submitButton = dom.offerForm.querySelector("button[type='submit']");
  if (!state.currentUser) {
    syncUrgencyLimit(null);
    document.querySelector("#credits").value = "0";
    document.querySelector("#bid-value").value = "0";
    document.querySelector("#bid-value").disabled = true;
    submitButton.disabled = true;
    dom.offerAccount.textContent = "Log in to make an offer with your own credits.";
    showStatus(dom.walletStatus, "Please log in before requesting a resource.", true);
    return;
  }

  if (!resource || !user || !isOpen(resource)) {
    syncUrgencyLimit(user);
    document.querySelector("#credits").value = "0";
    document.querySelector("#bid-value").value = "0";
    document.querySelector("#bid-value").disabled = true;
    submitButton.disabled = true;
    showStatus(dom.walletStatus, "Choose an open listing to see wallet credits.");
    return;
  }

  if (resource.ownerUserId === user.id) {
    syncUrgencyLimit(user);
    document.querySelector("#credits").value = "0";
    document.querySelector("#bid-value").value = "0";
    document.querySelector("#bid-value").disabled = true;
    submitButton.disabled = true;
    dom.offerAccount.textContent = `${user.name} is the owner of this resource.`;
    showStatus(dom.walletStatus, "You cannot make an offer on your own resource.", true);
    return;
  }

  const modeInput = document.querySelector("#mode");
  const bidInput = document.querySelector("#bid-value");
  const creditsInput = document.querySelector("#credits");
  const baseCreditScore = Math.min(user.availableCredits, 50);

  syncUrgencyLimit(user);
  dom.offerAccount.textContent = `Logged in as ${user.name}`;
  modeInput.value = resource.mode;
  modeInput.disabled = true;
  creditsInput.value = String(baseCreditScore);
  bidInput.disabled = resource.mode === "Exchange";
  bidInput.required = resource.mode === "Bidding";
  bidInput.max = String(user.availableCredits);
  submitButton.disabled = false;
  if (resource.mode === "Exchange") {
    bidInput.value = "0";
  }

  const semesterName = state.semester?.name || "active semester";
  showStatus(
    dom.walletStatus,
    `${user.name}: ${user.availableCredits} available credits (${user.lockedCredits} locked). High priority left: ${user.highUrgencyRemaining ?? 0}/2 for ${semesterName}.`
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
    fragment.querySelector(".winner").textContent = closed ? (resource.allocatedTo || "Closed") : "Pending";

    const badge = fragment.querySelector(".badge");
    badge.textContent = closed ? (resource.allocatedTo ? `Allocated: ${resource.bestScore}` : "Deadline passed") : resource.urgency;
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

function setCurrentUser(user) {
  state.currentUser = user;
  state.offerContextKey = null;
  clearRequestResult();
  if (user) {
    localStorage.setItem("smartExchangeUser", JSON.stringify(user));
  } else {
    localStorage.removeItem("smartExchangeUser");
  }
  updateAccountUI();
  syncOfferMode();
}

function updateAccountUI() {
  const freshUser = selectedUser();
  if (state.currentUser && freshUser) {
    state.currentUser = { ...state.currentUser, ...freshUser };
    localStorage.setItem("smartExchangeUser", JSON.stringify(state.currentUser));
  }

  document.body.classList.toggle("is-authenticated", Boolean(state.currentUser));
  document.querySelectorAll("[data-view-target]").forEach((control) => {
    const target = control.dataset.viewTarget;
    if (control.classList.contains("brand-button")) return;
    control.disabled = !state.currentUser && isProtectedView(target);
  });

  if (!state.currentUser) {
    dom.accountTitle.textContent = "No account logged in";
    dom.accountSummary.textContent = "Log in to list resources, request items, and use your own semester credits.";
    dom.logoutButton.classList.add("hidden");
    document.querySelector("#listing-owner").value = "";
    document.querySelector("#listing-owner").placeholder = "Log in first";
    dom.offerAccount.textContent = "Log in to make an offer with your own credits.";
    return;
  }

  const user = state.currentUser;
  const semesterName = state.semester?.name || "active semester";
  dom.accountTitle.textContent = user.name;
  dom.accountSummary.textContent = `${user.email || "Student account"} - ${user.availableCredits} available credits, ${user.lockedCredits} locked for ${semesterName}.`;
  dom.logoutButton.classList.remove("hidden");
  document.querySelector("#listing-owner").value = user.name;
  dom.offerAccount.textContent = `Logged in as ${user.name}`;
}

async function refreshData() {
  const response = await fetch("/api/resources");
  const payload = await response.json();
  state.resources = payload.resources || [];
  state.internships = payload.internships || [];
  state.users = payload.users || [];
  state.semester = payload.semester || null;
  renderResources();
  updateAccountUI();
  renderInternships();
}

async function restoreSession() {
  if (!state.currentUser?.authToken) return;
  const response = await fetch("/api/session", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ authToken: state.currentUser.authToken })
  });
  if (!response.ok) {
    setCurrentUser(null);
    return;
  }
  const data = await response.json();
  setCurrentUser(data.user);
}

dom.offerForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (!state.currentUser) {
    showStatus(dom.offerStatus, "Please log in before making an offer.", true);
    openView("auth");
    return;
  }

  const payload = {
    resourceId: Number(dom.resourceSelect.value),
    authToken: state.currentUser.authToken,
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

  const highRemaining = data.highUrgencyRemaining ?? selectedUser()?.highUrgencyRemaining ?? 0;
  showStatus(dom.offerStatus, `Offer accepted. Priority score: ${data.score}. Available credits: ${data.availableCredits}. High priority left: ${highRemaining}/2.`);
  await refreshData();
});

dom.listingForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (!state.currentUser) {
    showStatus(dom.listingStatus, "Please log in before listing a resource.", true);
    openView("auth");
    return;
  }

  const payload = {
    title: document.querySelector("#listing-title").value.trim(),
    authToken: state.currentUser.authToken,
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

dom.registerForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const response = await fetch("/api/register", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      name: document.querySelector("#register-name").value.trim(),
      email: document.querySelector("#register-email").value.trim(),
      password: document.querySelector("#register-password").value
    })
  });
  const data = await response.json();
  if (!response.ok) {
    showStatus(dom.registerStatus, data.error || "Account could not be created.", true);
    return;
  }
  dom.registerForm.reset();
  clearStatus(dom.loginStatus);
  setCurrentUser(data.user);
  showStatus(dom.registerStatus, "Account created and logged in.");
  await refreshData();
  openView(state.pendingView || "dashboard");
  state.pendingView = null;
});

dom.loginForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const response = await fetch("/api/login", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      email: document.querySelector("#login-email").value.trim(),
      password: document.querySelector("#login-password").value
    })
  });
  const data = await response.json();
  if (!response.ok) {
    showStatus(dom.loginStatus, data.error || "Could not log in.", true);
    return;
  }
  dom.loginForm.reset();
  clearStatus(dom.registerStatus);
  setCurrentUser(data.user);
  showStatus(dom.loginStatus, "Logged in successfully.");
  await refreshData();
  openView(state.pendingView || "dashboard");
  state.pendingView = null;
});

dom.logoutButton.addEventListener("click", () => {
  setCurrentUser(null);
  showStatus(dom.loginStatus, "Logged out.");
  openView("auth", { replaceHistory: true });
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

document.querySelectorAll("[data-view-target]").forEach((control) => {
  control.addEventListener("click", () => openView(control.dataset.viewTarget));
});

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

async function initializeApp() {
  updateAccountUI();
  await restoreSession();
  if (state.currentUser) {
    await refreshData();
  }
  const initialView = viewFromLocation();
  history.replaceState({ viewId: initialView }, "", viewUrl(initialView));
  openView(initialView, { updateHistory: false, scroll: false });
}

initializeApp()
  .catch(() => {
    showStatus(dom.loginStatus, "Could not connect to the C++ backend.", true);
    openView("auth", { replaceHistory: true });
  });
