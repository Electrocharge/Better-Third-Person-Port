#pragma once

// Original vector icons for the Better Third Person HUD button (not taken from FreeLook).
namespace btp::icons {

inline constexpr char kDisabledSvg[] = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="56" height="56" viewBox="0 0 56 56">
  <circle cx="28" cy="28" r="25" fill="#1b1f24" fill-opacity="0.75" stroke="#e8edf2" stroke-width="2.5"/>
  <circle cx="28" cy="19" r="5" fill="none" stroke="#e8edf2" stroke-width="2.5"/>
  <path d="M19 38 C19 29 37 29 37 38" fill="none" stroke="#e8edf2" stroke-width="2.5" stroke-linecap="round"/>
  <path d="M10 28 A18 18 0 0 1 28 10" fill="none" stroke="#e8edf2" stroke-width="2.5" stroke-linecap="round" stroke-dasharray="3 4"/>
  <path d="M46 28 A18 18 0 0 1 28 46" fill="none" stroke="#e8edf2" stroke-width="2.5" stroke-linecap="round" stroke-dasharray="3 4"/>
</svg>
)SVG";

inline constexpr char kEnabledSvg[] = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="56" height="56" viewBox="0 0 56 56">
  <circle cx="28" cy="28" r="25" fill="#143d27" fill-opacity="0.85" stroke="#4ade80" stroke-width="3"/>
  <circle cx="28" cy="19" r="5" fill="#4ade80" stroke="#4ade80" stroke-width="2.5"/>
  <path d="M19 38 C19 29 37 29 37 38" fill="none" stroke="#4ade80" stroke-width="3" stroke-linecap="round"/>
  <path d="M10 28 A18 18 0 0 1 28 10" fill="none" stroke="#4ade80" stroke-width="3" stroke-linecap="round"/>
  <path d="M46 28 A18 18 0 0 1 28 46" fill="none" stroke="#4ade80" stroke-width="3" stroke-linecap="round"/>
  <path d="M24 8.5 L29 10 L25 14" fill="none" stroke="#4ade80" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M32 47.5 L27 46 L31 42" fill="none" stroke="#4ade80" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
)SVG";

} // namespace btp::icons
