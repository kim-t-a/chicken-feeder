// Kenyan Poultry Standards (Kenya Bureau of Standards / Poultry Association of Kenya)
// daily_feed_grams_per_bird per age week

export const FEED_REQUIREMENTS = [
  // ── BROILER (Cobb 500 / Ross 308) ──────────────────────────────
  { breed: 'broiler', age_weeks: 1,  daily_feed_grams_per_bird: 12  },
  { breed: 'broiler', age_weeks: 2,  daily_feed_grams_per_bird: 28  },
  { breed: 'broiler', age_weeks: 3,  daily_feed_grams_per_bird: 52  },
  { breed: 'broiler', age_weeks: 4,  daily_feed_grams_per_bird: 82  },
  { breed: 'broiler', age_weeks: 5,  daily_feed_grams_per_bird: 110 },
  { breed: 'broiler', age_weeks: 6,  daily_feed_grams_per_bird: 134 },
  { breed: 'broiler', age_weeks: 7,  daily_feed_grams_per_bird: 152 },
  { breed: 'broiler', age_weeks: 8,  daily_feed_grams_per_bird: 165 },

  // ── LAYER (Lohmann Brown / Hy-Line) ────────────────────────────
  // Chick starter (1–8 weeks)
  { breed: 'layer', age_weeks: 1,  daily_feed_grams_per_bird: 10  },
  { breed: 'layer', age_weeks: 2,  daily_feed_grams_per_bird: 18  },
  { breed: 'layer', age_weeks: 3,  daily_feed_grams_per_bird: 28  },
  { breed: 'layer', age_weeks: 4,  daily_feed_grams_per_bird: 38  },
  { breed: 'layer', age_weeks: 5,  daily_feed_grams_per_bird: 48  },
  { breed: 'layer', age_weeks: 6,  daily_feed_grams_per_bird: 55  },
  { breed: 'layer', age_weeks: 7,  daily_feed_grams_per_bird: 62  },
  { breed: 'layer', age_weeks: 8,  daily_feed_grams_per_bird: 68  },
  // Grower (9–18 weeks)
  { breed: 'layer', age_weeks: 9,  daily_feed_grams_per_bird: 72  },
  { breed: 'layer', age_weeks: 10, daily_feed_grams_per_bird: 76  },
  { breed: 'layer', age_weeks: 11, daily_feed_grams_per_bird: 78  },
  { breed: 'layer', age_weeks: 12, daily_feed_grams_per_bird: 80  },
  { breed: 'layer', age_weeks: 13, daily_feed_grams_per_bird: 83  },
  { breed: 'layer', age_weeks: 14, daily_feed_grams_per_bird: 86  },
  { breed: 'layer', age_weeks: 15, daily_feed_grams_per_bird: 88  },
  { breed: 'layer', age_weeks: 16, daily_feed_grams_per_bird: 90  },
  { breed: 'layer', age_weeks: 17, daily_feed_grams_per_bird: 95  },
  { breed: 'layer', age_weeks: 18, daily_feed_grams_per_bird: 100 },
  // Layer (19+ weeks)
  { breed: 'layer', age_weeks: 19, daily_feed_grams_per_bird: 110 },
  { breed: 'layer', age_weeks: 20, daily_feed_grams_per_bird: 115 },
  // weeks 21–52 → 115–120 g/day (flat)
  ...Array.from({ length: 32 }, (_, i) => ({
    breed: 'layer', age_weeks: 21 + i, daily_feed_grams_per_bird: 118,
  })),

  // ── BROILER-BREEDER ─────────────────────────────────────────────
  { breed: 'broiler-breeder', age_weeks: 1,  daily_feed_grams_per_bird: 15  },
  { breed: 'broiler-breeder', age_weeks: 2,  daily_feed_grams_per_bird: 30  },
  { breed: 'broiler-breeder', age_weeks: 3,  daily_feed_grams_per_bird: 50  },
  { breed: 'broiler-breeder', age_weeks: 4,  daily_feed_grams_per_bird: 72  },
  { breed: 'broiler-breeder', age_weeks: 5,  daily_feed_grams_per_bird: 90  },
  { breed: 'broiler-breeder', age_weeks: 6,  daily_feed_grams_per_bird: 108 },
  { breed: 'broiler-breeder', age_weeks: 7,  daily_feed_grams_per_bird: 120 },
  { breed: 'broiler-breeder', age_weeks: 8,  daily_feed_grams_per_bird: 130 },
  ...Array.from({ length: 10 }, (_, i) => ({
    breed: 'broiler-breeder', age_weeks: 9 + i, daily_feed_grams_per_bird: 135 + i * 2,
  })),
  ...Array.from({ length: 30 }, (_, i) => ({
    breed: 'broiler-breeder', age_weeks: 19 + i, daily_feed_grams_per_bird: 155,
  })),

  // ── KIENYEJI (Local Indigenous Breed) ───────────────────────────
  { breed: 'kienyeji', age_weeks: 1,  daily_feed_grams_per_bird: 8   },
  { breed: 'kienyeji', age_weeks: 2,  daily_feed_grams_per_bird: 15  },
  { breed: 'kienyeji', age_weeks: 3,  daily_feed_grams_per_bird: 22  },
  { breed: 'kienyeji', age_weeks: 4,  daily_feed_grams_per_bird: 30  },
  { breed: 'kienyeji', age_weeks: 5,  daily_feed_grams_per_bird: 38  },
  { breed: 'kienyeji', age_weeks: 6,  daily_feed_grams_per_bird: 44  },
  { breed: 'kienyeji', age_weeks: 7,  daily_feed_grams_per_bird: 50  },
  { breed: 'kienyeji', age_weeks: 8,  daily_feed_grams_per_bird: 55  },
  ...Array.from({ length: 10 }, (_, i) => ({
    breed: 'kienyeji', age_weeks: 9 + i, daily_feed_grams_per_bird: 58 + i,
  })),
  ...Array.from({ length: 30 }, (_, i) => ({
    breed: 'kienyeji', age_weeks: 19 + i, daily_feed_grams_per_bird: 75,
  })),
];

/**
 * Look up daily feed per bird from local seed first, then Supabase table.
 * @param {string} breed
 * @param {number} ageWeeks
 * @returns {number} grams/bird/day or null if not found
 */
export function lookupFeedRequirement(breed, ageWeeks) {
  const row = FEED_REQUIREMENTS.find(
    (r) => r.breed === breed && r.age_weeks === ageWeeks
  );
  return row ? row.daily_feed_grams_per_bird : null;
}

export const BREED_OPTIONS = [
  { value: 'broiler',         label: 'Broiler (Cobb/Ross)' },
  { value: 'layer',           label: 'Layer (Lohmann/Hy-Line)' },
  { value: 'broiler-breeder', label: 'Broiler-Breeder' },
  { value: 'kienyeji',        label: 'Kienyeji (Local Breed)' },
  { value: 'other',           label: 'Other' },
];
