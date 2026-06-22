-- ============================================================
-- PoultryPal — Supabase SQL Setup Script
-- Run this in your Supabase SQL Editor (Dashboard → SQL Editor)
-- ============================================================

-- ── REAL-TIME BOWL WEIGHT (run this if already set up the tables) ──
-- Adds the live weight column that the ESP32 writes every 5 seconds.
-- Safe to run even if the column already exists.
ALTER TABLE public.feeder_config
  ADD COLUMN IF NOT EXISTS current_bowl_weight float DEFAULT 0;
GRANT ALL ON public.feeder_config TO anon, authenticated, service_role;
-- ──────────────────────────────────────────────────────────────────



-- ── STEP 1: Create all tables first ──────────────────────────

CREATE TABLE IF NOT EXISTS public.feeder_config (
  id                      uuid DEFAULT gen_random_uuid() PRIMARY KEY,
  mode                    text DEFAULT 'scheduled',
  breed                   text DEFAULT 'broiler',
  chicken_count           int  DEFAULT 50,
  age_weeks               int  DEFAULT 4,
  adlib_threshold_grams   float DEFAULT 300,
  target_dispense_grams   float DEFAULT 0,
  calibration_factor      float DEFAULT 1.0,
  calibration_trigger     boolean DEFAULT false,
  known_weight_grams      float DEFAULT 100,
  tare_trigger            boolean DEFAULT false,
  motor_enabled           boolean DEFAULT true,
  jam_detected            boolean DEFAULT false,
  alert_cause             text DEFAULT '',
  alert_message           text DEFAULT '',
  scheduled_feed_trigger  boolean DEFAULT false,
  slot_count              int DEFAULT 2,
  feed_time_0             text DEFAULT '07:00',
  feed_time_1             text DEFAULT '17:00',
  feed_time_2             text DEFAULT '',
  feed_time_3             text DEFAULT '',
  feed_time_4             text DEFAULT '',
  feed_time_5             text DEFAULT '',
  feed_time_6             text DEFAULT '',
  feed_time_7             text DEFAULT '',
  updated_at              timestamptz DEFAULT now()
);

CREATE TABLE IF NOT EXISTS public.feeding_logs (
  id                uuid DEFAULT gen_random_uuid() PRIMARY KEY,
  amount_dispensed  float NOT NULL DEFAULT 0,
  current_weight    float NOT NULL DEFAULT 0,
  logged_at         timestamptz DEFAULT now()
);

CREATE TABLE IF NOT EXISTS public.hopper_refills (
  id          uuid DEFAULT gen_random_uuid() PRIMARY KEY,
  added_grams float NOT NULL,
  notes       text  DEFAULT '',
  refilled_at timestamptz DEFAULT now()
);

CREATE TABLE IF NOT EXISTS public.alert_logs (
  id            uuid DEFAULT gen_random_uuid() PRIMARY KEY,
  alert_cause   text NOT NULL,
  alert_message text DEFAULT '',
  logged_at     timestamptz DEFAULT now(),
  cleared_at    timestamptz,
  is_cleared    boolean DEFAULT false
);

CREATE TABLE IF NOT EXISTS public.feed_requirements (
  breed                     text NOT NULL,
  age_weeks                 int  NOT NULL,
  daily_feed_grams_per_bird float NOT NULL,
  PRIMARY KEY (breed, age_weeks)
);


-- ── STEP 2: Enable Realtime (tables must exist first) ─────────

ALTER PUBLICATION supabase_realtime ADD TABLE public.feeder_config;
ALTER PUBLICATION supabase_realtime ADD TABLE public.feeding_logs;
ALTER PUBLICATION supabase_realtime ADD TABLE public.alert_logs;


-- ── STEP 3: Seed feed_requirements (Kenyan Poultry Standards) ─

INSERT INTO public.feed_requirements (breed, age_weeks, daily_feed_grams_per_bird)
VALUES
  -- Broiler (Cobb 500 / Ross 308)
  ('broiler', 1, 12), ('broiler', 2, 28), ('broiler', 3, 52),
  ('broiler', 4, 82), ('broiler', 5, 110), ('broiler', 6, 134),
  ('broiler', 7, 152), ('broiler', 8, 165),
  -- Layer starter (1-8 weeks)
  ('layer', 1, 10), ('layer', 2, 18), ('layer', 3, 28),
  ('layer', 4, 38), ('layer', 5, 48), ('layer', 6, 55),
  ('layer', 7, 62), ('layer', 8, 68),
  -- Layer grower (9-18 weeks)
  ('layer', 9, 72), ('layer', 10, 76), ('layer', 11, 78),
  ('layer', 12, 80), ('layer', 13, 83), ('layer', 14, 86),
  ('layer', 15, 88), ('layer', 16, 90), ('layer', 17, 95),
  ('layer', 18, 100),
  -- Layer production (19-52 weeks)
  ('layer', 19, 110), ('layer', 20, 115),
  ('layer', 21, 118), ('layer', 22, 118), ('layer', 23, 118),
  ('layer', 24, 118), ('layer', 25, 118), ('layer', 26, 118),
  ('layer', 27, 118), ('layer', 28, 118), ('layer', 29, 118),
  ('layer', 30, 118), ('layer', 31, 118), ('layer', 32, 118),
  ('layer', 33, 118), ('layer', 34, 118), ('layer', 35, 118),
  ('layer', 36, 118), ('layer', 37, 118), ('layer', 38, 118),
  ('layer', 39, 118), ('layer', 40, 118), ('layer', 41, 118),
  ('layer', 42, 118), ('layer', 43, 118), ('layer', 44, 118),
  ('layer', 45, 118), ('layer', 46, 118), ('layer', 47, 118),
  ('layer', 48, 118), ('layer', 49, 118), ('layer', 50, 118),
  ('layer', 51, 118), ('layer', 52, 118),
  -- Broiler-Breeder
  ('broiler-breeder', 1, 15), ('broiler-breeder', 2, 30),
  ('broiler-breeder', 3, 50), ('broiler-breeder', 4, 72),
  ('broiler-breeder', 5, 90), ('broiler-breeder', 6, 108),
  ('broiler-breeder', 7, 120), ('broiler-breeder', 8, 130),
  ('broiler-breeder', 9, 135), ('broiler-breeder', 10, 137),
  ('broiler-breeder', 11, 139), ('broiler-breeder', 12, 141),
  ('broiler-breeder', 13, 143), ('broiler-breeder', 14, 145),
  ('broiler-breeder', 15, 147), ('broiler-breeder', 16, 149),
  ('broiler-breeder', 17, 151), ('broiler-breeder', 18, 153),
  ('broiler-breeder', 19, 155), ('broiler-breeder', 20, 155),
  -- Kienyeji (local indigenous breed)
  ('kienyeji', 1, 8),  ('kienyeji', 2, 15), ('kienyeji', 3, 22),
  ('kienyeji', 4, 30), ('kienyeji', 5, 38), ('kienyeji', 6, 44),
  ('kienyeji', 7, 50), ('kienyeji', 8, 55),
  ('kienyeji', 9, 58),  ('kienyeji', 10, 59), ('kienyeji', 11, 60),
  ('kienyeji', 12, 61), ('kienyeji', 13, 62), ('kienyeji', 14, 63),
  ('kienyeji', 15, 64), ('kienyeji', 16, 65), ('kienyeji', 17, 66),
  ('kienyeji', 18, 67), ('kienyeji', 19, 75), ('kienyeji', 20, 75)
ON CONFLICT (breed, age_weeks) DO NOTHING;


-- ── STEP 4: Disable RLS + Grant anon access ───────────────────
-- Single-user prototype — no Supabase Auth yet.
-- Both the web app AND the ESP32 use the anon key.

ALTER TABLE public.feeder_config     DISABLE ROW LEVEL SECURITY;
ALTER TABLE public.feeding_logs      DISABLE ROW LEVEL SECURITY;
ALTER TABLE public.hopper_refills    DISABLE ROW LEVEL SECURITY;
ALTER TABLE public.alert_logs        DISABLE ROW LEVEL SECURITY;
ALTER TABLE public.feed_requirements DISABLE ROW LEVEL SECURITY;

GRANT ALL ON public.feeder_config      TO anon, authenticated, service_role;
GRANT ALL ON public.feeding_logs       TO anon, authenticated, service_role;
GRANT ALL ON public.hopper_refills     TO anon, authenticated, service_role;
GRANT ALL ON public.alert_logs         TO anon, authenticated, service_role;
GRANT ALL ON public.feed_requirements  TO anon, authenticated, service_role;


-- ── Done! ─────────────────────────────────────────────────────
-- Refresh your app at http://localhost:3000
-- You should see the onboarding screen to set up your feeder.


-- ============================================================
-- FOR LATER: RLS with per-user policies (when you add Auth)
-- Uncomment this block ONLY after adding Supabase Auth login.
-- ============================================================
/*
ALTER TABLE public.feeder_config  ADD COLUMN IF NOT EXISTS user_id uuid REFERENCES auth.users(id);
ALTER TABLE public.feeding_logs   ADD COLUMN IF NOT EXISTS user_id uuid REFERENCES auth.users(id);
ALTER TABLE public.hopper_refills ADD COLUMN IF NOT EXISTS user_id uuid REFERENCES auth.users(id);
ALTER TABLE public.alert_logs     ADD COLUMN IF NOT EXISTS user_id uuid REFERENCES auth.users(id);

ALTER TABLE public.feeder_config  ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.feeding_logs   ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.hopper_refills ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.alert_logs     ENABLE ROW LEVEL SECURITY;

CREATE POLICY "own feeder config"   ON public.feeder_config  FOR ALL USING (auth.uid() = user_id);
CREATE POLICY "own feeding logs"    ON public.feeding_logs   FOR ALL USING (auth.uid() = user_id);
CREATE POLICY "own hopper refills"  ON public.hopper_refills FOR ALL USING (auth.uid() = user_id);
CREATE POLICY "own alert logs"      ON public.alert_logs     FOR ALL USING (auth.uid() = user_id);

ALTER TABLE public.feed_requirements ENABLE ROW LEVEL SECURITY;
CREATE POLICY "read feed requirements" ON public.feed_requirements FOR SELECT USING (true);
*/
