#pragma once
// Genetic Lab: a patch is treated as 13 parameter groups (the Master Mutate groups).
// A child takes each whole group from parent A or parent B, then gets a light mutation.
// Every patch involved is kept in an archive with its parents, so the family tree can
// be walked back through the generations.
#include <JuceHeader.h>
#include <vector>
#include "../Mut/Mutator.h"

namespace tg
{

struct LabPatch
{
    juce::String id, name, parentA, parentB;
    int generation = 0;
    std::vector<float> values;     // plain parameter values (P_COUNT)
    juce::var routes;              // Mod Matrix routes (inherited with the Modulation group)
    uint32_t fromB = 0;            // for children: bit g set = group g came from parent B
};

class GeneticLab
{
public:
    static constexpr int kChildren = 8;

    const LabPatch* find (const juce::String& id) const
    {
        for (auto& p : archive) if (p.id == id) return &p;
        return nullptr;
    }

    // Adds (or replaces) a patch in the archive; returns its id.
    juce::String add (LabPatch p)
    {
        if (p.id.isEmpty()) p.id = juce::Uuid().toString().substring (0, 8);
        for (auto& q : archive) if (q.id == p.id) { q = p; return p.id; }
        archive.push_back (std::move (p));
        while (archive.size() > 400) archive.erase (archive.begin());   // oldest first
        return archive.back().id;
    }

    // Breeds kChildren children from the two parents. variation = light mutation amount (0..0.5).
    // Returns the new generation's index in 'broods'.
    int breed (const juce::String& idA, const juce::String& idB, float variation, uint32_t seed)
    {
        const LabPatch* a = find (idA);
        const LabPatch* b = find (idB);
        if (a == nullptr || b == nullptr) return -1;
        const LabPatch A = *a, B = *b;   // copies: add() below may reallocate the archive
        const int gen = std::max (A.generation, B.generation) + 1;
        std::vector<juce::String> ids;
        for (int k = 0; k < kChildren; ++k)
        {
            const uint32_t cs = seed * 2654435761u + (uint32_t) k * 40503u + 1u;
            uint32_t mask = 0;
            for (int g = 0; g < ML_COUNT; ++g) if (hash01 (cs ^ (uint32_t) (g * 7919 + 13)) < 0.5f) mask |= 1u << g;
            const uint32_t all = (1u << ML_COUNT) - 1u;
            if (mask == 0) mask = 1u << (cs % ML_COUNT);               // never an exact copy of a parent
            if (mask == all) mask &= ~(1u << ((cs >> 8) % ML_COUNT));
            LabPatch c = cross (A, B, mask);
            if (variation > 0.0f)
            {
                MutationTable t; t.build (cs);
                applyMutation (t, variation, 0, c.values.data());
            }
            c.generation = gen;
            c.parentA = A.id; c.parentB = B.id;
            c.name = "Gen " + juce::String (gen) + " / " + juce::String (k + 1);
            ids.push_back (add (std::move (c)));
        }
        broods.push_back ({ A.id, B.id, ids });
        return (int) broods.size() - 1;
    }

    // The child of a and b that takes group g from b when bit g of mask is set (no mutation).
    static LabPatch cross (const LabPatch& a, const LabPatch& b, uint32_t mask)
    {
        LabPatch c;
        c.values = a.values;
        for (int i = 0; i < (int) c.values.size() && i < (int) b.values.size(); ++i)
        {
            const int g = geneGroupOf (i);
            if (g >= 0 && ((mask >> g) & 1u)) c.values[(size_t) i] = b.values[(size_t) i];
        }
        c.routes = ((mask >> ML_Mod) & 1u) ? b.routes : a.routes;
        c.fromB = mask;
        return c;
    }

    // "Gen 3 / 5  <-  A: Gen 2 / 1 (<- ...), B: Bass" : the line of ancestors, n levels deep
    juce::String lineage (const juce::String& id, int depth = 3) const
    {
        const LabPatch* p = find (id);
        if (p == nullptr) return "?";
        juce::String s = p->name;
        if (depth > 0 && (p->parentA.isNotEmpty() || p->parentB.isNotEmpty()))
            s << "  <-  [" << lineage (p->parentA, depth - 1) << "  x  " << lineage (p->parentB, depth - 1) << "]";
        return s;
    }

    struct Brood { juce::String parentA, parentB; std::vector<juce::String> children; };
    std::vector<LabPatch> archive;
    std::vector<Brood> broods;
    juce::String parentA, parentB, current;

    juce::var toVar() const
    {
        auto* root = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        for (auto& p : archive)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", p.id); o->setProperty ("name", p.name);
            o->setProperty ("parentA", p.parentA); o->setProperty ("parentB", p.parentB);
            o->setProperty ("generation", p.generation); o->setProperty ("fromB", (int) p.fromB);
            auto* vals = new juce::DynamicObject();
            for (int i = 0; i < (int) p.values.size() && i < P_COUNT; ++i) if (geneGroupOf (i) >= 0) vals->setProperty (kParamIds[i], p.values[(size_t) i]);
            o->setProperty ("values", juce::var (vals));
            o->setProperty ("routes", p.routes);
            arr.add (juce::var (o));
        }
        root->setProperty ("archive", arr);
        juce::Array<juce::var> bs;
        for (auto& b : broods)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("parentA", b.parentA); o->setProperty ("parentB", b.parentB);
            juce::Array<juce::var> ch; for (auto& c : b.children) ch.add (c);
            o->setProperty ("children", ch);
            bs.add (juce::var (o));
        }
        root->setProperty ("broods", bs);
        root->setProperty ("parentA", parentA); root->setProperty ("parentB", parentB); root->setProperty ("current", current);
        return juce::var (root);
    }

    // defaults: the values parameters take when a stored patch doesn't mention them
    void fromVar (const juce::var& v, const std::vector<float>& defaults)
    {
        archive.clear(); broods.clear(); parentA = parentB = current = {};
        if (! v.isObject()) return;
        if (auto* arr = v["archive"].getArray())
            for (auto& o : *arr)
            {
                LabPatch p;
                p.id = o["id"].toString(); p.name = o["name"].toString();
                p.parentA = o["parentA"].toString(); p.parentB = o["parentB"].toString();
                p.generation = (int) o["generation"]; p.fromB = (uint32_t) (int) o["fromB"];
                p.values = defaults;
                if (auto* vals = o["values"].getDynamicObject())
                    for (int i = 0; i < P_COUNT; ++i)
                        if (vals->hasProperty (kParamIds[i])) p.values[(size_t) i] = (float) (double) vals->getProperty (kParamIds[i]);
                p.routes = o["routes"];
                archive.push_back (std::move (p));
            }
        if (auto* bs = v["broods"].getArray())
            for (auto& o : *bs)
            {
                Brood b; b.parentA = o["parentA"].toString(); b.parentB = o["parentB"].toString();
                if (auto* ch = o["children"].getArray()) for (auto& c : *ch) b.children.push_back (c.toString());
                broods.push_back (b);
            }
        parentA = v["parentA"].toString(); parentB = v["parentB"].toString(); current = v["current"].toString();
    }

private:
    static float hash01 (uint32_t x)
    {
        x += 0x9E3779B9u; x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
        return (x & 0xFFFFFF) / 16777216.0f;
    }
};

} // namespace tg
