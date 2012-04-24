/*
 * To change this template, choose Tools | Templates
 * and open the template in the editor.
 */
package upnp;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 *
 * @author dill
 */
public class FSM {

    static Map<String, String> ents = new HashMap<String, String>();

    public static void main(String[] args) throws Exception{
        ents.put("&amp;", "&");
        ents.put("&apos;", "'");
        ents.put("&lt;", "<");
        ents.put("&gt;", ">");
        ents.put("&quot;", "\"");
        ents.put(new String("æ".getBytes("utf-8"), "iso-8859-1"), "a");
        ents.put(new String("ø".getBytes("utf-8"), "iso-8859-1"), "o");
        ents.put(new String("å".getBytes("utf-8"), "iso-8859-1"), "a");
        ents.put(new String("Æ".getBytes("utf-8"), "iso-8859-1"), "A");
        ents.put(new String("Ø".getBytes("utf-8"), "iso-8859-1"), "O");
        ents.put(new String("Å".getBytes("utf-8"), "iso-8859-1"), "A");
        ents.put(new String("ä".getBytes("utf-8"), "iso-8859-1"), "a");
        ents.put(new String("ö".getBytes("utf-8"), "iso-8859-1"), "o");
        ents.put(new String("Ä".getBytes("utf-8"), "iso-8859-1"), "A");
        ents.put(new String("Ö".getBytes("utf-8"), "iso-8859-1"), "O");
        String t = createCTable(ents);
        System.out.println("const char* fsm = \"" + t + "\";");
    }

    private static String createCTable(Map<String, String> ents) {
        Tree t = new Tree("-");
        for (String key : ents.keySet()) {
            Tree.Node curNode = t.root;
            for ( char c : key.toCharArray() ) {
                Tree.Node next = curNode.findChild(c);
                if ( next == null )
                    next = curNode.addChild(c);
                curNode = next;
            }
            curNode.setRes(ents.get(key));
        }
        StringBuilder sb = new StringBuilder();
        int c =0;
        for ( int i : t.fill() )
            switch(c++ %3) {
                case 0:
//                    sb.append((char)i);
//                    break;
                case 1:
                case 2:
                    sb.append("\\x" + new BigInteger(""+i).toString(16));// +"\"\"");
                    //sb.append("(" + i +")");
                    break;
            }
           
        return sb.toString();
    }

    public static class Tree {

        private Node root;

        public Tree(String rootData) {
            root = new Node(rootData, null, new ArrayList<Node>());
        }
 
        public List<Integer> fill() {
            List<Integer> res = root.fill();
            return res.subList(3, res.size()); // strip useless root
        }
        private static class Node implements Comparable<Node> {

            private Node(String data, Node parent, List<Node> children) {
                this.data = data;
                this.parent = parent;
                this.children = children;
            }
            private String data;
            private Node parent;
            private List<Node> children;
            private String res;
            
            public String getData() {
                return data;
            }

            public Node getParent() {
                return parent;
            }

            public List<Node> getChildren() {
                return children;
            }

            public Node addChild(String data) {
                Node n = new Node(data, this, new ArrayList<Node>());
                children.add(n);
                Collections.sort(children);
                return n;
            }
            
            public Node addChild(char data) {
                return addChild("" + data);
            }
            
            public Node findChild( String data ) {
                for ( Node n : children )
                    if ( data.equals(n.data) )
                        return n;
                return null;
            }
            public Node findChild( char data ) {
                return findChild("" + data);
            }

            public String getRes() {
                return res;
            }

            public void setRes(String res) {
                this.res = res;
            }
 
            public List<Integer> fill() {
                List<Integer> me = new ArrayList<Integer>();
                me.add((int)data.charAt(0));
                me.add(0);
                me.add(res==null?0:(int)res.charAt(0));
                int c = 0;
                for( Node n: children ){
                    List<Integer> ch = n.fill();
                    if ( ++c == children.size())
                        ch.set(1, 0);
                    me.addAll(ch);
                }
                me.set(1, me.size());
                return me;
            }

            @Override
            public int compareTo(Node o) {
                return data.compareTo(o.data);
            }

        }
    }
}
