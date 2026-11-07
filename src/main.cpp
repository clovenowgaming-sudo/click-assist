#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

static double g_now = 0.0;
static double g_clickAt = -100.0;
static double g_clickX = 0.0;
static double g_prevX = 0.0;
static double g_speed = 0.0;
static double g_lastReport = -100.0;
static double g_lastSave = -100.0;

static char const* frameKey(PlayerObject* p) {
	if (p->m_isShip) return "ship-frames";
	if (p->m_isBird) return "ufo-frames";
	if (p->m_isBall) return "ball-frames";
	if (p->m_isDart) return "wave-frames";
	if (p->m_isRobot) return "robot-frames";
	if (p->m_isSpider) return "spider-frames";
	if (p->m_isSwing) return "swing-frames";
	return "cube-frames";
}

static int bufferFrames(PlayerObject* p) {
	if (!p) return 0;
	return static_cast<int>(Mod::get()->getSettingValue<int64_t>(frameKey(p)));
}

static double frameLen() {
	auto fps = Mod::get()->getSettingValue<int64_t>("frame-rate");
	if (fps < 1) fps = 1;
	return 1.0 / static_cast<double>(fps);
}

static std::string fmtFrames(double sec) {
	return fmt::format("{:.1f} frames ({:.0f} ms)", sec / frameLen(), sec * 1000.0);
}

static void note(std::string const& text) {
	log::info("[ClickAssist] {}", text);
	if (!Mod::get()->getSettingValue<bool>("notify")) return;
	Notification::create(text, NotificationIcon::None, 0.9f)->show();
}

static CCRect shrinkRect(CCRect r, float scale) {
	float w = r.size.width * scale;
	float h = r.size.height * scale;
	return CCRect(r.getMidX() - w / 2.f, r.getMidY() - h / 2.f, w, h);
}

static bool rectsOverlap(CCRect const& a, CCRect const& b) {
	return a.getMinX() <= b.getMaxX() && a.getMaxX() >= b.getMinX()
		&& a.getMinY() <= b.getMaxY() && a.getMaxY() >= b.getMinY();
}

struct PadState {
	bool userDown = false;
	bool gameDown = false;
	bool pending = false;
	bool armed = false;
	bool fell = false;
	bool wasGround = true;
	double pressAt = 0.0;
	double releaseAt = 0.0;
	double leftAt = 0.0;
};

class $modify(CAGameLayer, GJBaseGameLayer) {
	struct Fields {
		double now = 0.0;
		PadState pads[2];
	};

	bool active() {
		if (!Mod::get()->getSettingValue<bool>("enabled")) return false;
		return typeinfo_cast<PlayLayer*>(this) != nullptr;
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (button != 1 || !this->active()) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}
		auto player = isPlayer1 ? m_player1 : m_player2;
		if (!player) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		auto f = m_fields.self();
		auto& s = f->pads[isPlayer1 ? 0 : 1];

		if (down) {
			if (isPlayer1) {
				g_clickAt = f->now;
				g_clickX = player->getPositionX();
			}
			s.userDown = true;
			s.pending = false;

			if (!player->m_isOnGround && s.fell && (f->now - s.leftAt) <= 0.25) {
				note("Late click: " + fmtFrames(f->now - s.leftAt));
				s.fell = false;
			}

			s.armed = !player->m_isOnGround;
			s.pressAt = f->now;

			if (s.gameDown) {
				GJBaseGameLayer::handleButton(false, 1, isPlayer1);
			}
			s.gameDown = true;
			GJBaseGameLayer::handleButton(true, 1, isPlayer1);
			return;
		}

		s.userDown = false;
		int frames = bufferFrames(player);
		if (frames > 0 && s.gameDown) {
			double hold = frames * frameLen();
			double elapsed = f->now - s.pressAt;
			if (elapsed < hold) {
				s.pending = true;
				s.releaseAt = s.pressAt + hold;
				return;
			}
		}
		s.pending = false;
		s.gameDown = false;
		GJBaseGameLayer::handleButton(false, 1, isPlayer1);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
		if (!this->active()) return;

		auto f = m_fields.self();
		f->now += dt;
		g_now = f->now;

		if (m_player1 && dt > 0.f) {
			double x = m_player1->getPositionX();
			if (x > g_prevX) g_speed = (x - g_prevX) / dt;
			g_prevX = x;
		}

		for (int i = 0; i < 2; i++) {
			auto p = (i == 0) ? m_player1 : m_player2;
			if (!p) continue;
			bool isP1 = (i == 0);
			auto& s = f->pads[i];
			bool g = p->m_isOnGround;

			if (s.pending && f->now >= s.releaseAt) {
				s.pending = false;
				s.gameDown = false;
				GJBaseGameLayer::handleButton(false, 1, isP1);
			}

			if (g && !s.wasGround) {
				s.fell = false;
				if (s.armed && s.gameDown) {
					double early = f->now - s.pressAt;
					if (early <= 0.5) {
						note("Early click: " + fmtFrames(early) + (s.userDown ? " (held)" : " (buffered)"));
					}
					if (p->m_isBall || p->m_isSpider) {
						GJBaseGameLayer::handleButton(false, 1, isP1);
						GJBaseGameLayer::handleButton(true, 1, isP1);
						s.pressAt = f->now;
						if (s.pending) {
							s.releaseAt = std::max(s.releaseAt, f->now + 2.0 * frameLen());
						}
					}
				}
				s.armed = false;
			} else if (!g && s.wasGround) {
				s.leftAt = f->now;
				s.fell = !s.gameDown;
			}

			s.wasGround = g;
		}
	}
};

class $modify(CAPlayLayer, PlayLayer) {
	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		auto mod = Mod::get();
		if (mod->getSettingValue<bool>("enabled")
			&& mod->getSettingValue<bool>("hitbox-enabled")
			&& player && obj && !player->m_isDead) {

			bool isHazard = obj->m_objectType == GameObjectType::Hazard;
			bool isSolid = obj->m_objectType == GameObjectType::Solid;
			bool blocks = mod->getSettingValue<bool>("shrink-blocks");

			if (isHazard || (isSolid && blocks)) {
				float hs = static_cast<float>(mod->getSettingValue<int64_t>("hazard-scale")) / 100.f;
				float ps = static_cast<float>(mod->getSettingValue<int64_t>("player-scale")) / 100.f;
				CCRect pr = shrinkRect(player->getObjectRect(), ps);
				CCRect orr = shrinkRect(obj->getObjectRect(), hs);
				if (!rectsOverlap(pr, orr)) {
					if (g_now - g_lastSave > 0.4) {
						g_lastSave = g_now;
						note("Hitbox saved you");
					}
					return;
				}
			}
		}

		PlayLayer::destroyPlayer(player, obj);

		if (!mod->getSettingValue<bool>("enabled")) return;
		if (player != m_player1) return;
		if (g_now - g_lastReport < 0.5) return;
		g_lastReport = g_now;

		double since = g_now - g_clickAt;
		std::string msg;
		if (since > 2.0 || g_clickAt < 0.0) {
			msg = "Died: no click in the last 2 seconds";
		} else {
			msg = "Died " + fmtFrames(since) + " after your click";
			if (obj && g_speed > 1.0) {
				double dx = obj->getPositionX() - g_clickX;
				double framesAhead = dx / g_speed / frameLen();
				if (framesAhead >= 0.0) {
					msg += fmt::format(" | Hazard was {:.1f} frames ahead when you clicked", framesAhead);
				} else {
					msg += fmt::format(" | You clicked {:.1f} frames AFTER the hazard", -framesAhead);
				}
			}
		}
		note(msg);
	}
};
