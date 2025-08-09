#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <cmath>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <iostream>

struct Element {
    std::string name;
    std::string symbol;
    int atomicNumber;
    sf::Color color;
};

static const std::vector<Element> ELEMENTS = {
    {"Hydrogen","H",1, sf::Color(200,200,255)},
    {"Helium","He",2, sf::Color(255,200,200)},
    {"Lithium","Li",3, sf::Color(200,255,200)},
    {"Beryllium","Be",4, sf::Color(200,255,255)},
    {"Boron","B",5, sf::Color(255,220,180)},
    {"Carbon","C",6, sf::Color(180,180,180)},
    {"Nitrogen","N",7, sf::Color(150,200,255)},
    {"Oxygen","O",8, sf::Color(255,120,120)},
    {"Sodium","Na",11, sf::Color(255,255,120)},
    {"Chlorine","Cl",17, sf::Color(120,255,120)}
};

struct Electron {
    float radius;
    float angle;
    float speed; // radians/sec
};

struct Atom {
    int id;
    int elementIndex;
    sf::Vector2f pos;
    float nucleusRadius = 16.f;
    bool active = false;
    bool selected = false;
    std::vector<Electron> electrons;
    std::optional<float> scheduledStart; // seconds since sim start
};

struct Link {
    int aId;
    int bId;
};

struct Button {
    sf::RectangleShape box;
    sf::Text label;
    std::function<void()> onClick;
    bool hover = false;
};

static const float SIDEBAR_W = 320.f;

sf::Text makeText(const std::string& s, const sf::Font& font, unsigned size, sf::Color color, sf::Vector2f pos) {
    sf::Text t;
    t.setFont(font);
    t.setString(s);
    t.setCharacterSize(size);
    t.setFillColor(color);
    t.setPosition(pos);
    return t;
}

Button makeButton(const std::string& label, const sf::Font& font, const sf::Vector2f& pos, const sf::Vector2f& size, std::function<void()> onClick) {
    Button b;
    b.box.setPosition(pos);
    b.box.setSize(size);
    b.box.setFillColor(sf::Color(40,40,50));
    b.box.setOutlineThickness(1.f);
    b.box.setOutlineColor(sf::Color(90,90,110));
    b.label = makeText(label, font, 16, sf::Color::White, {pos.x + 12, pos.y + 8});
    b.onClick = std::move(onClick);
    return b;
}

bool pointInRect(const sf::Vector2f& p, const sf::FloatRect& r) {
    return r.contains(p);
}

float length(const sf::Vector2f& v) {
    return std::sqrt(v.x*v.x + v.y*v.y);
}

sf::Vector2f clampToCanvas(const sf::Vector2f& p, const sf::Vector2u& winSize) {
    float x = std::clamp(p.x, SIDEBAR_W + 20.f, (float)winSize.x - 20.f);
    float y = std::clamp(p.y, 20.f, (float)winSize.y - 20.f);
    return {x, y};
}

std::vector<Electron> makeElectronsForElement(int atomicNumber) {
    // Simple, visual-only distribution over shells (not physically accurate)
    // Shell radii and max electrons per shell (2, 8, 18, 32...) — we’ll cap visually
    std::vector<int> shellCap = {2, 8, 8, 18}; // simplified caps
    std::vector<float> radii   = {30.f, 50.f, 70.f, 90.f};

    std::vector<Electron> e;
    int remaining = std::min(atomicNumber, 24); // cap for rendering
    std::srand((unsigned)std::time(nullptr));

    for (size_t s = 0; s < shellCap.size() && remaining > 0; ++s) {
        int inShell = std::min(remaining, shellCap[s]);
        for (int i = 0; i < inShell; ++i) {
            float angle = (2.f * 3.1415926f * i) / std::max(1, inShell);
            float speed = (s % 2 == 0 ? 0.8f : -0.5f) * (1.f - s*0.1f);
            speed += ((std::rand() % 100) / 100.f - 0.5f) * 0.2f; // slight variance
            e.push_back({radii[s], angle, speed});
        }
        remaining -= inShell;
    }
    return e;
}

int main() {
    sf::RenderWindow window(sf::VideoMode(1200, 800), "Quantum Atom Sandbox");
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.loadFromFile("DejaVuSans.ttf")) {
        std::cerr << "Warning: DejaVuSans.ttf not found. Text will not render.\n";
    }

    // State
    std::vector<Atom> atoms;
    std::vector<Link> links;
    int nextId = 1;
    int selectedElement = 0;

    sf::Clock simClock;
    bool dragging = false;
    sf::Vector2f dragOffset;
    int draggingId = -1;

    // UI elements
    std::vector<Button> buttons;

    auto addAtom = [&](){
        const Element& el = ELEMENTS[selectedElement];
        Atom a;
        a.id = nextId++;
        a.elementIndex = selectedElement;
        a.pos = { SIDEBAR_W + 100.f + (float)(std::rand()%600), 100.f + (float)(std::rand()%500) };
        a.electrons = makeElectronsForElement(el.atomicNumber);
        a.active = false;
        a.selected = false;
        atoms.push_back(std::move(a));
    };

    auto removeSelected = [&](){
        std::vector<int> toRemoveIds;
        for (auto& a : atoms) if (a.selected) toRemoveIds.push_back(a.id);
        atoms.erase(std::remove_if(atoms.begin(), atoms.end(), [&](const Atom& a){
            return std::find(toRemoveIds.begin(), toRemoveIds.end(), a.id) != toRemoveIds.end();
        }), atoms.end());
        links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& L){
            return std::find(toRemoveIds.begin(), toRemoveIds.end(), L.aId) != toRemoveIds.end() ||
                   std::find(toRemoveIds.begin(), toRemoveIds.end(), L.bId) != toRemoveIds.end();
        }), links.end());
    };

    auto toggleActiveSelected = [&](){
        for (auto& a : atoms) if (a.selected) a.active = !a.active;
    };

    auto scheduleSelected = [&](){
        float t = simClock.getElapsedTime().asSeconds();
        for (auto& a : atoms) if (a.selected) a.scheduledStart = t + 2.0f; // +2s
    };

    auto clearAll = [&](){
        atoms.clear();
        links.clear();
    };

    auto linkPair = [&](){
        std::vector<int> sel;
        for (auto& a : atoms) if (a.selected) sel.push_back(a.id);
        if (sel.size() == 2) {
            // Avoid duplicates
            int a = sel[0], b = sel[1];
            if (a > b) std::swap(a,b);
            auto exists = std::any_of(links.begin(), links.end(), [&](const Link& L){ return L.aId==a && L.bId==b; });
            if (!exists) links.push_back({a,b});
        }
    };

    // Build buttons
    float x = 16.f, y = 20.f;
    buttons.push_back(makeButton("< Element", font, {x, y}, {140, 32}, [&](){
        selectedElement = (selectedElement - 1 + (int)ELEMENTS.size()) % (int)ELEMENTS.size();
    }));
    buttons.push_back(makeButton("Element >", font, {x + 160, y}, {140, 32}, [&](){
        selectedElement = (selectedElement + 1) % (int)ELEMENTS.size();
    }));
    y += 48;
    buttons.push_back(makeButton("Add Atom", font, {x, y}, {300, 32}, addAtom));
    y += 40;
    buttons.push_back(makeButton("Toggle Active", font, {x, y}, {300, 32}, toggleActiveSelected));
    y += 40;
    buttons.push_back(makeButton("Schedule +2s", font, {x, y}, {300, 32}, scheduleSelected));
    y += 40;
    buttons.push_back(makeButton("Link Pair", font, {x, y}, {300, 32}, linkPair));
    y += 40;
    buttons.push_back(makeButton("Remove Selected", font, {x, y}, {300, 32}, removeSelected));
    y += 40;
    buttons.push_back(makeButton("Clear All", font, {x, y}, {300, 32}, clearAll));
    y += 48;

    sf::Text elementsLabel = makeText("Elements:", font, 16, sf::Color(220,220,220), {16, y});
    y += 24;

    // Atom list starts at yList
    float yList = y;

    // Main loop
    while (window.isOpen()) {
        sf::Event ev;
        while (window.pollEvent(ev)) {
            if (ev.type == sf::Event::Closed) window.close();

            if (ev.type == sf::Event::MouseMoved) {
                sf::Vector2f m(ev.mouseMove.x, ev.mouseMove.y);
                for (auto& b : buttons) {
                    b.hover = pointInRect(m, b.box.getGlobalBounds());
                }
                if (dragging && draggingId != -1) {
                    for (auto& a : atoms) {
                        if (a.id == draggingId) {
                            a.pos = clampToCanvas(m - dragOffset, window.getSize());
                            break;
                        }
                    }
                }
            }

            if (ev.type == sf::Event::MouseButtonPressed && ev.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2f m(ev.mouseButton.x, ev.mouseButton.y);

                // Buttons
                bool clickedButton = false;
                for (auto& b : buttons) {
                    if (pointInRect(m, b.box.getGlobalBounds())) {
                        if (b.onClick) b.onClick();
                        clickedButton = true;
                        break;
                    }
                }
                if (clickedButton) continue;

                // Atom list selection (left panel)
                if (m.x < SIDEBAR_W) {
                    float yy = yList;
                    for (auto& a : atoms) {
                        sf::FloatRect rowRect(16.f, yy, SIDEBAR_W - 32.f, 22.f);
                        if (rowRect.contains(m)) {
                            if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) ||
                                sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
                                a.selected = !a.selected;
                            } else {
                                for (auto& z : atoms) z.selected = false;
                                a.selected = true;
                            }
                            break;
                        }
                        yy += 24.f;
                    }
                } else {
                    // Canvas: pick atom by proximity
                    int hitId = -1;
                    for (auto& a : atoms) {
                        if (length(m - a.pos) <= a.nucleusRadius + 8.f) {
                            hitId = a.id;
                        }
                    }
                    if (hitId != -1) {
                        if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) ||
                            sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
                            for (auto& a : atoms) if (a.id == hitId) a.selected = !a.selected;
                        } else {
                            for (auto& a : atoms) a.selected = (a.id == hitId);
                        }
                        // Prepare dragging
                        for (auto& a : atoms) {
                            if (a.id == hitId) {
                                dragging = true;
                                draggingId = hitId;
                                dragOffset = m - a.pos;
                                break;
                            }
                        }
                    } else {
                        // Clicked empty canvas: clear selection
                        for (auto& a : atoms) a.selected = false;
                    }
                }
            }

            if (ev.type == sf::Event::MouseButtonReleased && ev.mouseButton.button == sf::Mouse::Left) {
                dragging = false;
                draggingId = -1;
            }
        }

        // Time update
        float t = simClock.getElapsedTime().asSeconds();
        for (auto& a : atoms) {
            if (a.scheduledStart && t >= *a.scheduledStart) {
                a.active = true;
                a.scheduledStart.reset();
            }
            if (a.active) {
                for (auto& e : a.electrons) {
                    e.angle += e.speed * (1.f/60.f);
                }
            }
        }

        // Draw
        window.clear(sf::Color(12, 12, 16));

        // Sidebar
        sf::RectangleShape sidebar;
        sidebar.setPosition(0,0);
        sidebar.setSize({SIDEBAR_W, (float)window.getSize().y});
        sidebar.setFillColor(sf::Color(22,22,30));
        window.draw(sidebar);

        // Draw buttons
        for (auto& b : buttons) {
            b.box.setFillColor(b.hover ? sf::Color(55,55,70) : sf::Color(40,40,50));
            window.draw(b.box);
            if (font.getInfo().family != "") window.draw(b.label);
        }

        // Element display
        if (font.getInfo().family != "") {
            const auto& el = ELEMENTS[selectedElement];
            auto title = makeText("Selected: " + el.name + " (" + el.symbol + ")", font, 18, sf::Color::White, {16, 20+48+40+40+40+40+40+8});
            window.draw(title);
        }

        // Atom list
        float yy = yList;
        if (font.getInfo().family != "") window.draw(elementsLabel);
        for (auto& a : atoms) {
            const Element& el = ELEMENTS[a.elementIndex];
            sf::Text row = makeText(
                "ID " + std::to_string(a.id) + "  " + el.symbol + "  " + (a.active ? "[Active]" : "[Idle]"),
                font, 14, a.selected ? sf::Color(255,255,180) : sf::Color(200,200,210),
                {16, yy});
            if (font.getInfo().family != "") window.draw(row);
            yy += 24.f;
        }

        // Draw links (interactions)
        for (const auto& L : links) {
            const Atom* A = nullptr;
            const Atom* B = nullptr;
            for (const auto& a : atoms) {
                if (a.id == L.aId) A = &a;
                if (a.id == L.bId) B = &a;
            }
            if (A && B) {
                sf::Vertex line[] = {
                    sf::Vertex(A->pos, sf::Color(120,200,255)),
                    sf::Vertex(B->pos, sf::Color(120,200,255))
                };
                window.draw(line, 2, sf::Lines);
            }
        }

        // Draw atoms
        for (auto& a : atoms) {
            const Element& el = ELEMENTS[a.elementIndex];

            // Nucleus
            sf::CircleShape nucleus(a.nucleusRadius);
            nucleus.setOrigin(a.nucleusRadius, a.nucleusRadius);
            nucleus.setPosition(a.pos);
            nucleus.setFillColor(a.selected ? sf::Color(el.color.r, el.color.g, el.color.b, 255)
                                            : sf::Color(el.color.r, el.color.g, el.color.b, 220));
            nucleus.setOutlineThickness(a.active ? 3.f : 1.f);
            nucleus.setOutlineColor(a.active ? sf::Color(255,255,180) : sf::Color(90,90,110));
            window.draw(nucleus);

            // Orbits (rings)
            for (const auto& e : a.electrons) {
                sf::CircleShape orbit(e.radius);
                orbit.setOrigin(e.radius, e.radius);
                orbit.setPosition(a.pos);
                orbit.setFillColor(sf::Color(0,0,0,0));
                orbit.setOutlineThickness(1.f);
                orbit.setOutlineColor(sf::Color(60,60,70));
                window.draw(orbit);
            }

            // Electrons
            for (const auto& e : a.electrons) {
                float ex = a.pos.x + std::cos(e.angle) * e.radius;
                float ey = a.pos.y + std::sin(e.angle) * e.radius;
                sf::CircleShape electron(4.f);
                electron.setOrigin(4.f, 4.f);
                electron.setPosition(ex, ey);
                electron.setFillColor(a.active ? sf::Color(180,255,255) : sf::Color(160,180,200));
                window.draw(electron);
            }

            // Labels
            if (font.getInfo().family != "") {
                sf::Text label = makeText(ELEMENTS[a.elementIndex].symbol, font, 14, sf::Color::Black, {a.pos.x - 8, a.pos.y - 10});
                window.draw(label);
            }
        }

        window.display();
    }

    return 0;
}

