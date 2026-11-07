#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

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
	if (!Mod::get()->getSettingValue<bool>("notify")) return;
	Notification::create(text, NotificationIcon::None, 0.6f)->show();
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
			s.userDown = true;
			s.pending = false;

			if (!player->m_isOnGround && s.fell && (f->now - s.leftAt) <= 0.25) {
				note("Late click: " + fmtFrames(f->now - s.leftAt));
				s.fell = false;
			}

			s.armed = !player->m_isOnGround;
			s.pressAt = f->now;

			if (s.gameDown) {
				// game still thinks it is held, so release first to make a fresh click
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
