#include "stdafx.h"
#include "PlayerStats.h"
#include <algorithm>

bool PlayerStats::Dead() const
{
    return health_ <= 0;
}

int PlayerStats::Level() const
{
    return level_;
}

int PlayerStats::Experience() const
{
    return experience_;
}

int PlayerStats::NextExperience() const
{
    return 20 + (level_ - 1) * 12;
}

int PlayerStats::WeaponRank() const
{
    return weaponRank_;
}

int PlayerStats::Kills() const
{
    return kills_;
}

int PlayerStats::ChipsCollected() const
{
    return chipsCollected_;
}

float PlayerStats::Health() const
{
    return health_;
}

float PlayerStats::MaxHealth() const
{
    return 100.f + 18.f * (level_ - 1);
}

float PlayerStats::Damage() const
{
    return 12.f + 3.f * (level_ - 1) + 4.f * weaponRank_;
}

float PlayerStats::FireInterval() const
{
    return std::max(.22f, .85f / (1.f + .10f * (level_ - 1) + .05f * weaponRank_));
}

float PlayerStats::Range() const
{
    return std::min(420.f, 300.f + 8.f * (level_ - 1) + 5.f * weaponRank_);
}

float PlayerStats::MagnetRadius() const
{
    return std::min(300.f, 110.f + 8.f * (level_ - 1) + 6.f * weaponRank_);
}

float PlayerStats::MovementSpeed() const
{
    return 150.f + std::min(24.f, 2.f * (level_ - 1));
}

float PlayerStats::Invulnerability() const
{
    return invulnerability_;
}

float PlayerStats::ShotCooldown() const
{
    return fireTimer_;
}

bool PlayerStats::GainExperience(int amount)
{
    bool gained = false;
    experience_ = std::min(1000000, experience_ + amount);
    while (level_ < 30 && experience_ >= NextExperience())
    {
        experience_ -= NextExperience();
        ++level_;
        health_ = std::min(MaxHealth(), health_ + 30);
        gained = true;
    }
    if (level_ == 30)
    {
        experience_ = 0;
    }
    return gained;
}
