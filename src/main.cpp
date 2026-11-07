#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

class $modify(CAGameLayer, GJBaseGameLayer) {
	struct Fields {
		double now = 0.0;
		bool wasGround = true;
		bool pressedWhileGrounded = false;
		bool holding = false;
		bool fell = false;
		double leftGroundAt = 0.0;
		bool buffering = false;
		double pressAt = 0.0;
		int releaseSteps = 0;
	};

	bool inPlay() {
		if (!Mod::get()->getSettingValue<bool>("enabled")) return false;
		return typeinfo_cast<PlayLayer*>(this) != nullptr;
	}

	bool cubeLike() {
		auto p = m_player1;
		return p && !p->m_isShip && !p->m_isBird && !p->m_isBall && !p->m_isDart
			&& !p->m_isRobot && !p->m_isSpider && !p->m_isSwing;
	}

	std::string fmtTime(double sec) {
		double fps = static_cast<double>(Mod::get()->getSettingValue<int64_t>("frame-rate"));
		return fmt::format("{:.1f} frames ({:.0f} ms)", sec * fps, sec * 1000.0);
	}

	void note(std::string const& text) {
		if (!Mod::get()->getSettingValue<bool>("notify")) return;
		Notification::create(text, NotificationIcon::None, 0.7f)->show();
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (button != 1 || !isPlayer1 || !this->inPlay() || !this->cubeLike()) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}
		auto f = m_fields.self();
		auto p = m_player1;

		if (!down) {
			f->holding = false;
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		f->holding = true;
		bool assist = Mod::get()->getSettingValue<bool>("assist");

		if (p->m_isOnGround) {
			f->pressedWhileGrounded = true;
		} else {
			if (f->fell && (f->now - f->leftGroundAt) <= 0.3) {
				double late = f->now - f->leftGroundAt;
				double coyote = Mod::get()->getSettingValue<double>("coyote-ms") / 1000.0;
				f->fell = false;
				if (assist && late <= coyote) {
					this->note("Too late: " + this->fmtTime(late) + " (saved)");
					f->pressedWhileGrounded = true;
					p->m_isOnGround = true;
					GJBaseGameLayer::handleButton(down, button, isPlayer1);
					p->m_isOnGround = false;
					return;
				}
				this->note("Too late: " + this->fmtTime(late));
			}
			f->buffering = true;
			f->pressAt = f->now;
		}

		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
		if (!this->inPlay() || !this->cubeLike()) return;

		auto f = m_fields.self();
		auto p = m_player1;
		f->now += dt;

		if (f->releaseSteps > 0 && --f->releaseSteps == 0 && !f->holding) {
			GJBaseGameLayer::handleButton(false, 1, true);
		}

		bool g = p->m_isOnGround;

		if (g && !f->wasGround) {
			f->pressedWhileGrounded = false;
			f->fell = false;
			if (f->buffering) {
				f->buffering = false;
				double early = f->now - f->pressAt;
				if (early <= 0.3) {
					bool assist = Mod::get()->getSettingValue<bool>("assist");
					double buf = Mod::get()->getSettingValue<double>("buffer-ms") / 1000.0;
					if (assist && !f->holding && early <= buf) {
						this->note("Too early: " + this->fmtTime(early) + " (saved)");
						GJBaseGameLayer::handleButton(true, 1, true);
						f->releaseSteps = 2;
					} else {
						this->note("Too early: " + this->fmtTime(early));
					}
				}
			}
		} else if (!g && f->wasGround) {
			f->leftGroundAt = f->now;
			f->fell = !f->pressedWhileGrounded && !f->holding;
		}

		if (f->buffering) {
			double v = p->m_isUpsideDown ? -p->m_yVelocity : p->m_yVelocity;
			if (v > 0.0) f->buffering = false;
		}

		f->wasGround = g;
	}
};
