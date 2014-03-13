#ifndef OSMIUM_AREA_DETAIL_PROTO_RING
#define OSMIUM_AREA_DETAIL_PROTO_RING

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
#include <cassert>
#include <list>
#include <vector>

#include <osmium/osm/noderef.hpp>
#include <osmium/osm/ostream.hpp>
#include <osmium/area/segment.hpp>

namespace osmium {

    namespace area {

        namespace detail {

            /**
             * A ring in the process of being built by the Assembler object.
             */
            class ProtoRing {

                // nodes in this ring
                std::vector<osmium::NodeRef> m_nodes {};

                bool m_outer {true};

                // if this is an outer ring, these point to it's inner rings (if any)
                std::vector<ProtoRing*> m_inner {};

            public:

                ProtoRing(const NodeRefSegment& segment) {
                    add_location_end(segment.first());
                    add_location_end(segment.second());
                }

                ProtoRing(std::vector<osmium::NodeRef>::const_iterator nodes_begin, std::vector<osmium::NodeRef>::const_iterator nodes_end) {
                    for (auto it = nodes_begin; it != nodes_end; ++it) {
                        add_location_end(*it);
                    }
                }

                bool outer() const {
                    return m_outer;
                }

                void set_inner() {
                    m_outer = false;
                }

                std::vector<osmium::NodeRef>& nodes() {
                    return m_nodes;
                }

                const std::vector<osmium::NodeRef>& nodes() const {
                    return m_nodes;
                }

                void remove_nodes(std::vector<osmium::NodeRef>::iterator nodes_begin, std::vector<osmium::NodeRef>::iterator nodes_end) {
                    m_nodes.erase(nodes_begin, nodes_end);
                }

                void add_location_end(const osmium::NodeRef& nr) {
                    m_nodes.push_back(nr);
                }

                void add_location_start(const osmium::NodeRef& nr) {
                    m_nodes.insert(m_nodes.begin(), nr);
                }

                const osmium::NodeRef& first() const {
                    return m_nodes.front();
                }

                osmium::NodeRef& first() {
                    return m_nodes.front();
                }

                const osmium::NodeRef& last() const {
                    return m_nodes.back();
                }

                osmium::NodeRef& last() {
                    return m_nodes.back();
                }

                bool closed() const {
                    return m_nodes.front().location() == m_nodes.back().location();
                }

                int64_t sum() const {
                    int64_t sum = 0;

                    for (size_t i = 0; i < m_nodes.size(); ++i) {
                        size_t j = (i + 1) % m_nodes.size();
                        sum += static_cast<int64_t>(m_nodes[i].location().x()) * static_cast<int64_t>(m_nodes[j].location().y()) -
                               static_cast<int64_t>(m_nodes[j].location().x()) * static_cast<int64_t>(m_nodes[i].location().y());
                    }

                    return sum;
                }

                bool is_cw() const {
                    return sum() <= 0;
                }

                int64_t area() const {
                    return std::abs(sum()) / 2;
                }

                void swap_nodes(ProtoRing& other) {
                    std::swap(m_nodes, other.m_nodes);
                }

                void add_inner_ring(ProtoRing* ring) {
                    m_inner.push_back(ring);
                }

                const std::vector<ProtoRing*> inner_rings() const {
                    return m_inner;
                }

                void print(std::ostream& out) const {
                    out << "[";
                    bool first = true;
                    for (auto& nr : nodes()) {
                        if (!first) {
                            out << ',';
                        }
                        out << nr.ref();
                        first = false;
                    }
                    out << "]";
                }

                void reverse_nodes() {
                    std::reverse(m_nodes.begin(), m_nodes.end());
                }

                /**
                 * Merge other ring to end of this ring.
                 */
                void merge_ring(const ProtoRing& other, bool debug) {
                    if (debug) {
                        std::cerr << "        MERGE rings ";
                        print(std::cerr);
                        std::cerr << " to ";
                        other.print(std::cerr);
                        std::cerr << "\n";
                    }
                    for (auto ni = other.nodes().begin() + 1; ni != other.nodes().end(); ++ni) {
                        add_location_end(*ni);
                    }
                }

                void merge_ring_reverse(const ProtoRing& other, bool debug) {
                    if (debug) {
                        std::cerr << "        MERGE rings (reverse) ";
                        print(std::cerr);
                        std::cerr << " to ";
                        other.print(std::cerr);
                        std::cerr << "\n";
                    }
                    for (auto ni = other.nodes().rbegin() + 1; ni != other.nodes().rend(); ++ni) {
                        add_location_end(*ni);
                    }
                }

                const NodeRef& min_node() const {
                    return *std::min_element(nodes().begin(), nodes().end(), location_less());
                }

                bool is_in(ProtoRing* outer) {
                    osmium::Location testpoint = nodes().front().location();
                    bool is_in = false;

                    for (size_t i = 0, j = outer->nodes().size()-1; i < outer->nodes().size(); j = i++) {
                        if (((outer->nodes()[i].location().y() > testpoint.y()) != (outer->nodes()[j].location().y() > testpoint.y())) &&
                            (testpoint.x() < (outer->nodes()[j].location().x() - outer->nodes()[i].location().x()) * (testpoint.y() - outer->nodes()[i].location().y()) / (outer->nodes()[j].location().y() - outer->nodes()[i].location().y()) + outer->nodes()[i].location().x()) ) {
                            is_in = !is_in;
                        }
                    }

                    return is_in;
                }

            }; // class ProtoRing

            inline std::ostream& operator<<(std::ostream& out, const ProtoRing& ring) {
                ring.print(out);
                return out;
            }

        } // namespace detail

    } // namespace area

} // namespace osmium

#endif // OSMIUM_AREA_DETAIL_PROTO_RING
