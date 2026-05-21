const state = {
  resources: [],
  internships: []
};

const dom = {
  resourceList: document.querySelector("#resource-list"),
  internshipList: document.querySelector("#internship-list"),
  resourceSelect: document.querySelector("#resource-id"),
  form: document.querySelector("#offer-form"),
  status: document.querySelector("#status"),
  refresh: document.querySelector("#refresh-button"),
  activeCount: document.querySelector("#active-count"),
  offerCount: document.querySelector("#offer-count"),
  template: document.querySelector("#resource-template")
};

function showStatus(message, isError = false) {
  dom.status.textContent = message;
  dom.status.classList.toggle("error", isError);
  dom.status.classList.remove("hidden");
}

function formatDeadline(value) {
  const date = new Date(value * 1000);
  return new Intl.DateTimeFormat(undefined, {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  }).format(date);
}

function renderResources() {
  dom.resourceList.innerHTML = "";
  dom.resourceSelect.innerHTML = "";

  let active = 0;
  let offerTotal = 0;

  state.resources.forEach((resource) => {
    const isClosed = resource.allocatedTo !== "";
    if (!isClosed) {
      active += 1;
      const option = document.createElement("option");
      option.value = resource.id;
      option.textContent = resource.title;
      dom.resourceSelect.append(option);
    }
    offerTotal += resource.offerCount;

    const fragment = dom.template.content.cloneNode(true);
    fragment.querySelector(".resource-type").textContent = `${resource.type} by ${resource.owner}`;
    fragment.querySelector("h3").textContent = resource.title;
    fragment.querySelector(".description").textContent = resource.description;
    fragment.querySelector(".mode").textContent = resource.mode;
    fragment.querySelector(".deadline").textContent = formatDeadline(resource.deadline);
    fragment.querySelector(".offers").textContent = String(resource.offerCount);
    fragment.querySelector(".winner").textContent = isClosed ? resource.allocatedTo : "Pending";

    const badge = fragment.querySelector(".badge");
    badge.textContent = isClosed ? `Allocated: ${resource.bestScore}` : resource.urgency;
    badge.classList.toggle("closed", isClosed);

    dom.resourceList.append(fragment);
  });

  dom.activeCount.textContent = String(active);
  dom.offerCount.textContent = String(offerTotal);
}

function renderInternships() {
  dom.internshipList.innerHTML = "";
  state.internships.forEach((item) => {
    const article = document.createElement("article");
    article.innerHTML = `
      <h3>${item.company}</h3>
      <p>${item.title}</p>
      <p><strong>Deadline:</strong> ${item.deadline}</p>
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

dom.form.addEventListener("submit", async (event) => {
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
    showStatus(data.error || "Offer could not be submitted.", true);
    return;
  }

  showStatus(`Offer accepted. Priority score: ${data.score}`);
  await refreshData();
});

dom.refresh.addEventListener("click", async () => {
  await refreshData();
  showStatus("Listings refreshed. Expired resources are allocated automatically.");
});

refreshData().catch(() => {
  showStatus("Could not connect to the C++ backend.", true);
});
