#include "Module.h"

namespace burnish
{
int Module::indexOf (const std::string& id) const
{
    for (size_t i = 0; i < descs.size(); ++i)
        if (descs[i].id == id) return (int) i;
    return -1;
}

void Module::setDefaults()
{
    for (size_t i = 0; i < descs.size(); ++i)
        vals[i] = descs[i].def;
}

int Module::addFloat (const std::string& id, const std::string& name, float lo, float hi, float def,
                      const std::string& unit, int decimals, float skew)
{
    ParamDesc d;
    d.id = pid (id); d.name = pname (name);
    d.kind = ParamKind::Float; d.lo = lo; d.hi = hi; d.def = def; d.unit = unit; d.decimals = decimals; d.skew = skew;
    descs.push_back (d);
    vals.push_back (def);
    return (int) descs.size() - 1;
}

int Module::addBool (const std::string& id, const std::string& name, bool def)
{
    ParamDesc d;
    d.id = pid (id); d.name = pname (name);
    d.kind = ParamKind::Bool; d.lo = 0; d.hi = 1; d.def = def ? 1.0f : 0.0f;
    descs.push_back (d);
    vals.push_back (d.def);
    return (int) descs.size() - 1;
}

int Module::addChoice (const std::string& id, const std::string& name, std::vector<std::string> choices, int def)
{
    ParamDesc d;
    d.id = pid (id); d.name = pname (name);
    d.kind = ParamKind::Choice; d.lo = 0; d.hi = (float) choices.size() - 1; d.def = (float) def;
    d.choices = std::move (choices);
    descs.push_back (d);
    vals.push_back (d.def);
    return (int) descs.size() - 1;
}
} // namespace burnish
