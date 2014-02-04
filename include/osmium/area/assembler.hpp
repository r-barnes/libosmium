#ifndef OSMIUM_AREA_ASSEMBLER_HPP
#define OSMIUM_AREA_ASSEMBLER_HPP

/*

This file is part of Osmium (http://osmcode.org/osmium).

Copyright 2013,2014 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <algorithm>
#include <iostream>
#include <vector>

#include <osmium/memory/buffer.hpp>
#include <osmium/osm/area.hpp>
#include <osmium/osm/builder.hpp>
#include <osmium/osm/relation.hpp>

namespace osmium {

    namespace area {

        /**
         * Assembles area objects from multipolygon relations and their
         * members. This is called by the Collector object after all
         * members have been collected.
         */
        class Assembler {

            /**
             * Contains information about rings that are in the process
             * of being built by the Assembler object.
             */
            class RingInfo {

                std::vector<osmium::NodeRef> m_nodes;
                osmium::Box m_bounding_box;
                bool m_outer;

            public:

                RingInfo(const osmium::Way& way) : 
                    m_nodes(),
                    m_outer(true) {
                    add_nodes(way);
                    assert(!m_nodes.empty());
                }

                const std::vector<osmium::NodeRef>& nodes() const {
                    return m_nodes;
                }

                void add_nodes(const osmium::Way& way) {
//                    std::cerr << "add_nodes " << way.nodes().size() << "\n";
                    for (auto& nr : way.nodes()) {
                        m_nodes.push_back(nr);
                    }
                }

                osmium::NodeRef& last() {
                    return m_nodes.back();
                }

                bool closed() {
                    return m_nodes.front() == m_nodes.back();
                }

                bool outer() const {
                    return m_outer;
                }

                void outer(bool value) {
                    m_outer = value;
                }

                const osmium::Box& bounding_box() const {
                    return m_bounding_box;
                }

                void calculate_bounding_box() {
                    for (auto& node_ref : m_nodes) {
                        m_bounding_box.extend(node_ref.location());
                    }
                }

            }; // class RingInfo

            void build_rings(std::vector<size_t>& members, std::vector<RingInfo>& rings, osmium::memory::Buffer& buffer) {
//                std::cerr << "build_rings\n";
                if (members.empty()) { // XXX
                    return;
                }
                assert(!members.empty());
                RingInfo ri(buffer.get<const osmium::Way>(members.back()));
//                std::cerr << "  build_rings back\n";
                members.pop_back();
//                std::cerr << "  build_rings back 2\n";

                while (!ri.closed()) {
//                    std::cerr << "  not closed\n";
                    auto part = std::remove_if(members.begin(), members.end(), [&ri, &buffer](size_t pos) {
                        return buffer.get<const osmium::Way>(pos).nodes()[0] == ri.last();
                    });
                    if (part == members.end()) { // XXX andersrum
                        return; // XXX
                        throw std::runtime_error("illegal mp: can not close ring");
                    }
                    ri.add_nodes(buffer.get<const osmium::Way>(*part));
                    members.erase(part, members.end());
                }

                rings.push_back(ri);

//                std::cerr << "  build_rings recurse\n";
                build_rings(members, rings, buffer);
            }

        public:

            Assembler() {
            }

            void operator()(const osmium::Relation& relation, std::vector<size_t>& members, osmium::memory::Buffer& in_buffer, osmium::memory::Buffer& out_buffer) {
//                std::cerr << "build rel " << relation.id() << " members.size=" << members.size() << "\n";

                // erase ways without nodes
                members.erase(std::remove_if(members.begin(), members.end(), [&in_buffer](size_t pos){
                    return in_buffer.get<const osmium::Way>(pos).nodes().size() == 0;
                }), members.end());

                std::vector<RingInfo> rings;
                build_rings(members, rings, in_buffer);

                // find outer and inner rings
                for (auto& ring : rings) {
                    ring.calculate_bounding_box();
                }

                std::sort(rings.begin(), rings.end(), [](const RingInfo& a, const RingInfo& b){
                    return a.bounding_box().size() < b.bounding_box().size();
                });

                // XXX everything but first is inner (obviously wrong, but can fix later...)
                bool first = true;
                for (auto& ring : rings) {
                    if (!first) {
                        ring.outer(false);
                    }
                    first = false;
                }

                // create Area object
                osmium::osm::AreaBuilder builder(out_buffer);
                osmium::Area& area = builder.object();
                area.id(relation.id() * 2 + 1);
                area.version(relation.version());
                area.changeset(relation.changeset());
                area.timestamp(relation.timestamp());
                area.visible(relation.visible());
                area.uid(relation.uid());

                builder.add_user(relation.user());

                {
                    osmium::osm::TagListBuilder tl_builder(out_buffer, &builder);
                    for (const osmium::Tag& tag : relation.tags()) {
                        tl_builder.add_tag(tag.key(), tag.value());
                    }
                }

                for (auto& ring : rings) {
                    if (ring.outer()) {
                        osmium::osm::OuterRingBuilder ring_builder(out_buffer, &builder);
                        for (auto& node_ref : ring.nodes()) {
                            ring_builder.add_node_ref(node_ref);
                        }
                    } else {
                        osmium::osm::InnerRingBuilder ring_builder(out_buffer, &builder);
                        for (auto& node_ref : ring.nodes()) {
                            ring_builder.add_node_ref(node_ref);
                        }
                    }
                }

                out_buffer.commit();
            }

        }; // class Assembler

    } // namespace area

} // namespace osmium

#endif // OSMIUM_AREA_ASSEMBLER_HPP