(function () {
  "use strict";

  const search = document.querySelector("[data-docs-search]");
  const navLinks = Array.from(document.querySelectorAll("[data-docs-link]"));
  const empty = document.querySelector("[data-docs-empty]");
  const outlineLinks = Array.from(document.querySelectorAll("[data-outline-link]"));
  const sections = Array.from(document.querySelectorAll(".docs-section[id]"));

  if (search) {
    search.addEventListener("input", function () {
      const query = search.value.trim().toLowerCase();
      let visible = 0;

      navLinks.forEach(function (link) {
        const matches = !query || link.textContent.toLowerCase().includes(query) ||
          (link.dataset.keywords || "").toLowerCase().includes(query);
        link.hidden = !matches;
        if (matches) {
          visible += 1;
        }
      });

      document.querySelectorAll(".docs-nav-group").forEach(function (group) {
        const hasVisibleLink = Array.from(group.querySelectorAll("[data-docs-link]"))
          .some(function (link) { return !link.hidden; });
        group.hidden = !hasVisibleLink;
      });

      if (empty) {
        empty.classList.toggle("is-visible", visible === 0);
      }
    });

    document.addEventListener("keydown", function (event) {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k") {
        event.preventDefault();
        search.focus();
        search.select();
      }
    });
  }

  if (!("IntersectionObserver" in window) || sections.length === 0) {
    return;
  }

  const allTrackedLinks = navLinks.concat(outlineLinks);
  const setActive = function (id) {
    allTrackedLinks.forEach(function (link) {
      link.classList.toggle("is-active", link.getAttribute("href") === "#" + id);
    });
  };

  const observer = new IntersectionObserver(function (entries) {
    const visible = entries
      .filter(function (entry) { return entry.isIntersecting; })
      .sort(function (a, b) { return a.boundingClientRect.top - b.boundingClientRect.top; });
    if (visible.length > 0) {
      setActive(visible[0].target.id);
    }
  }, { rootMargin: "-90px 0px -70% 0px", threshold: 0 });

  sections.forEach(function (section) { observer.observe(section); });
}());
