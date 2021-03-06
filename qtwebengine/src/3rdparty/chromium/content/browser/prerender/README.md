This directory contains the experimental Prerender2 implementation
(https://crbug.com/1126305). This feature is now behind the
`blink::features::kPrerender2` flag.

If you're interested in relevant code changes, join the
prerendering-reviews@chromium.org group.

# Summary

Prerendering is "pre"-rendering, it's about pre-loading and rendering a page
before the user actually navigates to it. The main goal of prerendering is to
make the next page navigation faster, or ideally nearly instant.

The Prerender2 is the new implementation of prerendering.

# Terminology

- **Trigger**: "Trigger" is an entry point to start prerendering. Currently,
  `<link rel=prerender>` is the only trigger.
- **Activate**: The Prerender2 runs navigation code twice: navigation for
  prerendering a page, and navigation for displaying the prerendered page.
  "Activate" indicates the latter navigation.
- **Legacy Prerender**: "Legacy Prerender" is the previous implementation of
  prerendering that does not use NoStatePrefetch. This is already deprecated
  (https://crbug.com/755921).
- **NoStatePrefetch**: An internal mechanism for speculative prefetching of
  pages and their subresources that are on a critical path of page loading
  without executing any JavaScript or creating a complex state of the web
  platform (https://www.chromestatus.com/feature/5928321099497472). This
  mechanism is not purely "no state" because the HTTP cache allows to create
  cookies and other state related to validating cache entries. The current
  prerendering uses this mechanism, that is, it does not actually render pages,
  while the Prerender2 renders pages.

# References

The date is the publication date, not the last updated date.

- [Prerender2](https://docs.google.com/document/d/1P2VKCLpmnNm_cRAjUeE-bqLL0bslL_zKqiNeCzNom_w/edit?usp=sharing) (Oct, 2020): Introduces how Prerender2 works and more detailed designs such as Mojo Capability Control.
