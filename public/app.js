const state = {
  resources: [],
  internships: [],
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
  listingStatus: document.querySelector("#listing-status"),
  refresh: document.querySelector("#refresh-button"),
  activeCount: document.querySelector("#active-count"),
  offerCount: document.querySelector("#offer-count"),
  listedCount: document.querySelector("#listed-count"),
  template: document.querySelector("#resource-template"),
  search: document.querySelector("#search"),
  typeFilter: document.querySelector("#type-filter")
};

function openView(viewId) {
  document.querySelectorAll("[data-view]").forEach((view) => {
    view.classList.toggle("active", view.id === viewId);
  });

  document.querySelectorAll("[data-view-target]").forEach((control) => {
    control.classList.toggle("active", control.dataset.viewTarget === viewId);
  });

  window.scrollTo({ top: 0, behavior: "smooth" });
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
  openView("request");
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

async function refreshData() {
  const response = await fetch("/api/resources");
  const payload = await response.json();
  state.resources = payload.resources || [];
  state.internships = payload.internships || [];
  renderResources();
  renderInternships();
}

dom.offerForm.addEventListener("submit", async (event) => {
  event.preventDefault();

  const payload = {
    resourceId: Number(dom.resourceSelect.value),
    studentName: document.querySelector("#student-name").value.trim(),
    credits: Number(document.querySelector("#credits").value),
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

  showStatus(dom.offerStatus, `Offer accepted. Priority score: ${data.score}`);
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

document.querySelectorAll("[data-view-target]").forEach((control) => {
  control.addEventListener("click", () => openView(control.dataset.viewTarget));
});

refreshData().catch(() => {
  showStatus(dom.offerStatus, "Could not connect to the C++ backend.", true);
});
